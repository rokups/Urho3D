#define URHO3D_GEOMETRY_STATIC

#include "_Config.glsl"
#include "_Uniforms.glsl"
#include "_VertexLayout.glsl"
#include "_VertexTransform.glsl"
#include "_VertexScreenPos.glsl"
#include "_PixelOutput.glsl"

uniform sampler2D sDiffMap;

VERTEX_OUTPUT(vec2 vScreenPos)

#ifdef URHO3D_VERTEX_SHADER
void main()
{
    VertexTransform vertexTransform = GetVertexTransform();
    //mat4 modelMatrix = iModelMatrix;
    vec3 worldPos = vertexTransform.position;// GetWorldPos(modelMatrix);
    gl_Position = GetClipPos(worldPos);
    vScreenPos = GetScreenPosPreDiv(gl_Position);
}
#endif

#ifdef URHO3D_PIXEL_SHADER
void main()
{
    gl_FragColor = texture2D(sDiffMap, vScreenPos);
}
#endif

