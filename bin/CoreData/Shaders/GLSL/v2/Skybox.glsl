#ifndef UNLIT
    #define UNLIT
#endif

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
    mat4 modelMatrix = GetModelMatrix();
    vec4 worldPos = vec4(iPos.xyz, 0.0) * modelMatrix;
    worldPos.xyz += cCameraPos;
    worldPos.w = 1.0;
    gl_Position = worldPos * cViewProj;
    gl_Position.z = gl_Position.w;
    vTexCoord = iPos.xyz;
}
#endif

#ifdef URHO3D_PIXEL_SHADER
void main()
{
    gl_FragColor = cMatDiffColor * DiffMap_ToLight(textureCube(sDiffCubeMap, vTexCoord));
}
#endif
