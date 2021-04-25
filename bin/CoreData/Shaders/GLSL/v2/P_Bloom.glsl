#include "_Config.glsl"
#include "_Uniforms.glsl"
#include "_VertexLayout.glsl"
#include "_VertexTransform.glsl"
#include "_VertexScreenPos.glsl"
#include "_Samplers.glsl"
#include "_GammaCorrection.glsl"

VERTEX_OUTPUT_HIGHP(vec2 vTexCoord)
VERTEX_OUTPUT_HIGHP(vec2 vScreenPos)

#ifdef URHO3D_PIXEL_SHADER

UNIFORM_BUFFER_BEGIN(6, Custom)
    UNIFORM(half3 cLuminanceWeights)
    UNIFORM(half cIntensity)
    UNIFORM(vec2 cInputInvSize)
    UNIFORM(half2 cThreshold)
UNIFORM_BUFFER_END(6, Custom)

half FilterBrightness(half brightness)
{
    return clamp((brightness - cThreshold.x) * cThreshold.y, 0.0, 1.0);
}

half3 BrightFilter(half3 inputColor)
{
    half brightness = dot(inputColor, cLuminanceWeights);
    return inputColor * FilterBrightness(brightness);
}

#endif

#ifdef URHO3D_VERTEX_SHADER
void main()
{
    VertexTransform vertexTransform = GetVertexTransform();
    gl_Position = WorldToClipSpace(vertexTransform.position.xyz);
    vTexCoord = GetQuadTexCoord(gl_Position);
    vScreenPos = GetScreenPosPreDiv(gl_Position);
}
#endif

#ifdef URHO3D_PIXEL_SHADER
void main()
{
#ifdef BRIGHT
    half3 inputColor = texture2D(sDiffMap, vTexCoord).rgb;
    gl_FragColor = vec4(BrightFilter(inputColor), 1.0);
#endif

#ifdef BLURH
    half3 inputColor = texture2D(sDiffMap, vTexCoord + vec2(-2.0, 0.0) * cInputInvSize).rgb * 0.1;
    inputColor += texture2D(sDiffMap, vTexCoord + vec2(-1.0, 0.0) * cInputInvSize).rgb * 0.25;
    inputColor += texture2D(sDiffMap, vTexCoord + vec2(0.0, 0.0) * cInputInvSize).rgb * 0.3;
    inputColor += texture2D(sDiffMap, vTexCoord + vec2(1.0, 0.0) * cInputInvSize).rgb * 0.25;
    inputColor += texture2D(sDiffMap, vTexCoord + vec2(2.0, 0.0) * cInputInvSize).rgb * 0.1;
    gl_FragColor = vec4(inputColor, 1.0);
#endif

#ifdef BLURV
    half3 inputColor = texture2D(sDiffMap, vTexCoord + vec2(0.0, -2.0) * cInputInvSize).rgb * 0.1;
    inputColor += texture2D(sDiffMap, vTexCoord + vec2(0.0, -1.0) * cInputInvSize).rgb * 0.25;
    inputColor += texture2D(sDiffMap, vTexCoord + vec2(0.0, 0.0) * cInputInvSize).rgb * 0.3;
    inputColor += texture2D(sDiffMap, vTexCoord + vec2(0.0, 1.0) * cInputInvSize).rgb * 0.25;
    inputColor += texture2D(sDiffMap, vTexCoord + vec2(0.0, 2.0) * cInputInvSize).rgb * 0.1;
    gl_FragColor = vec4(inputColor, 1.0);
#endif

#ifdef COMBINE
    half3 bloom = cIntensity * texture2D(sDiffMap, vTexCoord).rgb;
    half brightness = clamp(dot(bloom, cLuminanceWeights), 0.0, 1.0);
    gl_FragColor = vec4(bloom, brightness);
#endif
}
#endif

