#include "_Config.glsl"
#include "_GammaCorrection.glsl"
#include "_SurfaceData.glsl"
#include "_BRDF.glsl"

#include "_Uniforms.glsl"
#include "_Samplers.glsl"
#include "_VertexLayout.glsl"
#include "_PixelOutput.glsl"

#include "_VertexTransform.glsl"
#include "_VertexScreenPos.glsl"
#include "_Fog.glsl"

VERTEX_OUTPUT(vec4 vScreenPos)
VERTEX_OUTPUT(vec2 vReflectUV)
VERTEX_OUTPUT(vec2 vWaterUV)
VERTEX_OUTPUT(vec4 vEyeVec)
VERTEX_OUTPUT(vec3 vNormal)

// TODO(renderer): Move to constants
const vec2 cNoiseSpeed = vec2(0.05, 0.05);
const float cNoiseTiling = 2.0;
const float cNoiseStrength = 0.02;
const float cFresnelPower = 8.0;
const vec3 cWaterTint = vec3(0.8, 0.8, 1.0);

#ifdef URHO3D_VERTEX_SHADER
void main()
{
    VertexTransform vertexTransform = GetVertexTransform();
    gl_Position = WorldToClipSpace(vertexTransform.position.xyz);
    vScreenPos = GetScreenPos(gl_Position);
    // GetQuadTexCoord() returns a vec2 that is OK for quad rendering; multiply it with output W
    // coordinate to make it work with arbitrary meshes such as the water plane (perform divide in pixel shader)
    // Also because the quadTexCoord is based on the clip position, and Y is flipped when rendering to a texture
    // on OpenGL, must flip again to cancel it out
    vReflectUV = GetQuadTexCoord(gl_Position);
    vReflectUV.y = 1.0 - vReflectUV.y;
    vReflectUV *= gl_Position.w;
    vWaterUV = iTexCoord * cNoiseTiling + cElapsedTime * cNoiseSpeed;
    vNormal = vertexTransform.normal;
    vEyeVec = vec4(cCameraPos - vertexTransform.position.xyz, GetDepth(gl_Position));
}
#endif

#ifdef URHO3D_PIXEL_SHADER
void main()
{
    vec2 refractUV = vScreenPos.xy / vScreenPos.w;
    vec2 reflectUV = vReflectUV.xy / vScreenPos.w;

    vec2 noise = (texture2D(sNormalMap, vWaterUV).rg - 0.5) * cNoiseStrength;
    refractUV += noise;
    // Do not shift reflect UV coordinate upward, because it will reveal the clipping of geometry below water
    if (noise.y < 0.0)
        noise.y = 0.0;
    reflectUV += noise;

    float fresnel = pow(1.0 - clamp(dot(normalize(vEyeVec.xyz), vNormal), 0.0, 1.0), cFresnelPower);
    vec3 refractColor = texture2D(sEmissiveMap, refractUV).rgb * cWaterTint;
    vec3 reflectColor = texture2D(sEnvMap, reflectUV).rgb;
    vec3 finalColor = mix(refractColor, reflectColor, fresnel);

    gl_FragColor = vec4(ApplyFog(finalColor, GetFogFactor(vEyeVec.w)), 1.0);
}
#endif
