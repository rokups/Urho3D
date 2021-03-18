#include "_Config.glsl"
#include "_Uniforms.glsl"
#include "_Samplers.glsl"
#include "_VertexLayout.glsl"
#include "_PixelOutput.glsl"

#include "_VertexTransform.glsl"
#include "_GammaCorrection.glsl"

VERTEX_OUTPUT(vec3 vTexCoord)

#ifdef URHO3D_VERTEX_SHADER
void main()
{
    VertexTransform vertexTransform = GetVertexTransform();
    gl_Position = WorldToClipSpace(vertexTransform.position.xyz);
    gl_Position.z = gl_Position.w;
    vTexCoord = iPos.xyz;
}
#endif

#ifdef URHO3D_PIXEL_SHADER
void main()
{
    vec4 sky = DiffMap_ToLight(cMatDiffColor * textureCube(sDiffCubeMap, vTexCoord));
    #ifdef HDRSCALE
        sky = pow(sky + clamp((cAmbientColor.a - 1.0) * 0.1, 0.0, 0.25), max(vec4(cAmbientColor.a), 1.0)) * clamp(cAmbientColor.a, 0.0, 1.0);
    #endif
    gl_FragColor = sky;
}
#endif
