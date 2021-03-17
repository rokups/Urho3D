#include "_Config.glsl"
#include "_Uniforms.glsl"
#include "_VertexLayout.glsl"
#include "_VertexTransform.glsl"
#include "_VertexScreenPos.glsl"
#include "_PixelOutput.glsl"
#include "_GammaCorrection.glsl"
#include "_Samplers.glsl"
#include "_DirectLighting.glsl"
#include "_Shadow.glsl"
#include "_Fog.glsl"
#include "_DeferredLighting.glsl"

VERTEX_OUTPUT(vec4 vScreenPos)
VERTEX_OUTPUT(vec3 vFarRay)
#ifdef URHO3D_ORTHOGRAPHIC_DEPTH
    VERTEX_OUTPUT(vec3 vNearRay)
#endif

#ifdef URHO3D_VERTEX_SHADER
void main()
{
    VertexTransform vertexTransform = GetVertexTransform();
    vec3 worldPos = vertexTransform.position.xyz;
    gl_Position = GetClipPos(worldPos);

    vScreenPos = GetDeferredScreenPos(gl_Position);
    vFarRay = GetDeferredFarRay(gl_Position);
    #ifdef URHO3D_ORTHOGRAPHIC_DEPTH
        vNearRay = GetDeferredNearRay(gl_Position);
    #endif
}
#endif

#ifdef URHO3D_PIXEL_SHADER
void main()
{
    float depth = ReconstructDepth(SampleGeometryBuffer(sDepthBuffer, vScreenPos).r);
    vec4 albedoInput = SampleGeometryBuffer(sDiffMap, vScreenPos);
    vec4 specularInput = SampleGeometryBuffer(sSpecMap, vScreenPos);
    vec4 normalInput = SampleGeometryBuffer(sNormalMap, vScreenPos);

    vec4 worldPos = GetDeferredWorldPos(vScreenPos, depth);
    vec3 eyeVec = normalize(-worldPos.xyz);
    worldPos.xyz += cCameraPos;

    vec3 normal = normalize(normalInput.rgb * 2.0 - 1.0);
    float specularPower = (1.0 - specularInput.a) * 255;

    DirectLightData lightData = GetDeferredDirectLightData(worldPos, depth);
    #ifdef URHO3D_LIGHT_HAS_SPECULAR
        vec3 finalColor = GetBlinnPhongDiffuseSpecular(lightData, normal, albedoInput.rgb,
            specularInput.rgb, eyeVec, specularPower);
    #else
        vec3 finalColor = GetBlinnPhongDiffuse(lightData, normal, albedoInput.rgb);
    #endif
    gl_FragColor = vec4(finalColor, 0.0);
}
#endif
