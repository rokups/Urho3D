#include "_Config.glsl"
#include "_Uniforms.glsl"
#include "_VertexLayout.glsl"
#include "_VertexTransform.glsl"
#include "_PixelOutput.glsl"
#include "Samplers.glsl"

#ifdef VSM_SHADOW
    VERTEX_SHADER_OUT(vec4 vTexCoord)
#else
    VERTEX_SHADER_OUT(vec2 vTexCoord)
#endif

#ifdef STAGE_VERTEX_SHADER
void main()
{
    mat4 modelMatrix = iModelMatrix;
    vec3 worldPos = GetWorldPos(modelMatrix);
    gl_Position = GetClipPos(worldPos);

    #ifdef NOUV
        vTexCoord.xy = vec2(0.0, 0.0);
    #else
        vTexCoord.xy = GetTexCoord(iTexCoord);
    #endif

    #ifdef VSM_SHADOW
        vTexCoord.zw = gl_Position.zw;
    #endif
}
#endif

#ifdef STAGE_PIXEL_SHADER
void main()
{
    #ifdef ALPHAMASK
        float alpha = texture2D(sDiffMap, vTexCoord.xy).a;
        if (alpha < 0.5)
            discard;
    #endif

    #ifdef VSM_SHADOW
        float depth = vTexCoord.z / vTexCoord.w * 0.5 + 0.5;
        gl_FragColor = vec4(depth, depth * depth, 1.0, 1.0);
    #else
        gl_FragColor = vec4(1.0);
    #endif
}
#endif
