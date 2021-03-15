#include "_Config.glsl"
#include "_Uniforms.glsl"
#include "_VertexLayout.glsl"
#include "_VertexTransform.glsl"
#include "_VertexScreenPos.glsl"
#include "_PixelOutput.glsl"
#include "_GammaCorrection.glsl"
#include "_Samplers.glsl"
#include "_Shadow.glsl"
#include "_PixelLighting.glsl"

#include "_Material.glsl"
#include "_Fog.glsl"

#ifdef DIRLIGHT
    VERTEX_OUTPUT(vec2 vScreenPos)
#else
    VERTEX_OUTPUT(vec4 vScreenPos)
#endif
VERTEX_OUTPUT(vec3 vFarRay)
#ifdef ORTHO
    VERTEX_OUTPUT(vec3 vNearRay)
#endif

#ifdef URHO3D_VERTEX_SHADER
void main()
{
    VertexTransform vertexTransform = GetVertexTransform();
    //mat4 modelMatrix = iModelMatrix;
    vec3 worldPos = vertexTransform.position.xyz;// GetWorldPos(modelMatrix);
    gl_Position = GetClipPos(worldPos);
    #ifdef DIRLIGHT
        vScreenPos = GetScreenPosPreDiv(gl_Position);
        vFarRay = GetFarRay(gl_Position);
        #ifdef ORTHO
            vNearRay = GetNearRay(gl_Position);
        #endif
    #else
        vScreenPos = GetScreenPos(gl_Position);
        vFarRay = GetFarRay(gl_Position) * gl_Position.w;
        #ifdef ORTHO
            vNearRay = GetNearRay(gl_Position) * gl_Position.w;
        #endif
    #endif
}
#endif

#ifdef URHO3D_PIXEL_SHADER
void main()
{
    // If rendering a directional light quad, optimize out the w divide
    #ifdef DIRLIGHT
        float depth = ReconstructDepth(texture2D(sDepthBuffer, vScreenPos).r);
        #ifdef ORTHO
            vec3 worldPos = mix(vNearRay, vFarRay, depth);
        #else
            vec3 worldPos = vFarRay * depth;
        #endif
        vec4 albedoInput = texture2D(sAlbedoBuffer, vScreenPos);
        vec4 normalInput = texture2D(sNormalBuffer, vScreenPos);
    #else
        float depth = ReconstructDepth(texture2DProj(sDepthBuffer, vScreenPos).r);
        #ifdef ORTHO
            vec3 worldPos = mix(vNearRay, vFarRay, depth) / vScreenPos.w;
        #else
            vec3 worldPos = vFarRay * depth / vScreenPos.w;
        #endif
        vec4 albedoInput = texture2DProj(sAlbedoBuffer, vScreenPos);
        vec4 normalInput = texture2DProj(sNormalBuffer, vScreenPos);
    #endif

    // Position acquired via near/far ray is relative to camera. Bring position to world space
    vec3 eyeVec = normalize(-worldPos);
    worldPos += cCameraPos;

    vec3 normal = normalize(normalInput.rgb * 2.0 - 1.0);
    vec4 projWorldPos = vec4(worldPos, 1.0);

    vec4 lightVec = NormalizeLightVector(GetLightVector(worldPos));
    float diff = GetDiffuseIntensity(normal, lightVec.xyz, lightVec.w);

    #ifdef SHADOW
        diff *= GetDeferredShadow(projWorldPos, depth);
    #endif

    #ifdef URHO3D_LIGHT_CUSTOM_SHAPE
        vec4 shapePos = projWorldPos * cLightShapeMatrix;
        vec3 lightColor = GetLightColorFromShape(shapePos);
    #else
        vec3 lightColor = GetLightColor(lightVec.xyz);
    #endif

    #ifdef SPECULAR
        float spec = GetSpecularIntensity(normal, eyeVec, lightVec.xyz, normalInput.a * 255.0);
        gl_FragColor = diff * vec4(lightColor * (albedoInput.rgb + spec * cLightColor.a * albedoInput.aaa), 0.0);
    #else
        gl_FragColor = diff * vec4(lightColor * albedoInput.rgb, 0.0);
    #endif
}
#endif
