#include "_Config.glsl"
#include "_Uniforms.glsl"
#include "_Samplers.glsl"
#include "_VertexLayout.glsl"

#include "_VertexTransform.glsl"
#include "_GammaCorrection.glsl"

VERTEX_OUTPUT_HIGHP(vec3 vTexCoord)

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
    gl_FragColor = DiffMap_ToLight(cMatDiffColor * textureCube(sDiffCubeMap, vTexCoord));
}
#endif
