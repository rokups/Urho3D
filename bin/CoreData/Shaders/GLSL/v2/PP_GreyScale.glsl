#define URHO3D_GEOMETRY_STATIC

#include "_Config.glsl"
#include "_Uniforms.glsl"
#include "_VertexLayout.glsl"
#include "_VertexTransform.glsl"
#include "_VertexScreenPos.glsl"
#include "_PixelOutput.glsl"
#include "_Samplers.glsl"
#include "Lighting.glsl"


VERTEX_OUTPUT(vec2 vScreenPos);

#ifdef URHO3D_VERTEX_SHADER
void main()
{
    VertexTransform vertexTransform = GetVertexTransform();
    gl_Position = GetClipPos(vertexTransform.position.xyz);
    vScreenPos = GetScreenPosPreDiv(gl_Position);
}
#endif

#ifdef URHO3D_PIXEL_SHADER
void main()
{
    vec3 rgb = texture2D(sDiffMap, vScreenPos).rgb;
    float intensity = GetIntensity(rgb);
    gl_FragColor = vec4(intensity, intensity, intensity, 1.0);
}
#endif
