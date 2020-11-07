#define GEOM_STATIC
#define LAYOUT_HAS_POSITION

#include "_Config.glsl"
#include "_Uniforms.glsl"
#include "_VertexLayout.glsl"
#include "_VertexTransform.glsl"
#include "_VertexScreenPos.glsl"
#include "_PixelOutput.glsl"

uniform sampler2D sDiffMap;

VERTEX_SHADER_OUT(vec2 vScreenPos)

#ifdef STAGE_VERTEX_SHADER
void main()
{
    mat4 modelMatrix = iModelMatrix;
    vec3 worldPos = GetWorldPos(modelMatrix);
    gl_Position = GetClipPos(worldPos);
    vScreenPos = GetScreenPosPreDiv(gl_Position);
}
#endif

#ifdef STAGE_PIXEL_SHADER
void main()
{
    gl_FragColor = texture2D(sDiffMap, vScreenPos);
}
#endif

