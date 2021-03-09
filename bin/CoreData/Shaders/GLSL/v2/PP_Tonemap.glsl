#define URHO3D_GEOMETRY_STATIC

#include "_Config.glsl"
#include "_Uniforms.glsl"
#include "_VertexLayout.glsl"
#include "_VertexTransform.glsl"
#include "_VertexScreenPos.glsl"
#include "_PixelOutput.glsl"
#include "_Samplers.glsl"
#include "PostProcess.glsl"

VERTEX_OUTPUT(vec2 vScreenPos);

#ifdef URHO3D_PIXEL_SHADER
    UNIFORM_BUFFER_BEGIN(6, Custom)
        UNIFORM(mediump float cTonemapExposureBias)
        UNIFORM(mediump float cTonemapMaxWhite)
    UNIFORM_BUFFER_END()
#endif

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
    #ifdef REINHARDEQ3
    vec3 color = ReinhardEq3Tonemap(max(texture2D(sDiffMap, vScreenPos).rgb * cTonemapExposureBias, 0.0));
    gl_FragColor = vec4(color, 1.0);
    #endif

    #ifdef REINHARDEQ4
    vec3 color = ReinhardEq4Tonemap(max(texture2D(sDiffMap, vScreenPos).rgb * cTonemapExposureBias, 0.0), cTonemapMaxWhite);
    gl_FragColor = vec4(color, 1.0);
    #endif

    #ifdef UNCHARTED2
    vec3 color = Uncharted2Tonemap(max(texture2D(sDiffMap, vScreenPos).rgb * cTonemapExposureBias, 0.0)) /
        Uncharted2Tonemap(vec3(cTonemapMaxWhite, cTonemapMaxWhite, cTonemapMaxWhite));
    gl_FragColor = vec4(color, 1.0);
    #endif
}
#endif

