#include "_Config.glsl"
#include "_Uniforms.glsl"
#include "_VertexLayout.glsl"
#include "_VertexTransform.glsl"
#include "_VertexScreenPos.glsl"
#include "_Samplers.glsl"

VERTEX_OUTPUT_HIGHP(vec2 vScreenPos)

#ifdef URHO3D_VERTEX_SHADER
void main()
{
    VertexTransform vertexTransform = GetVertexTransform();
    gl_Position = WorldToClipSpace(vertexTransform.position.xyz);
    vScreenPos = GetScreenPosPreDiv(gl_Position);
}
#endif

#ifdef URHO3D_PIXEL_SHADER
void main()
{
    const vec3 IntensityWeights = vec3(0.299, 0.587, 0.114);

    vec3 rgb = texture2D(sDiffMap, vScreenPos).rgb;
    float intensity = dot(rgb, IntensityWeights);
    gl_FragColor = vec4(intensity, intensity, intensity, 1.0);
}
#endif
