#include "_Config.glsl"
#include "_Uniforms.glsl"
#include "_VertexLayout.glsl"
#include "_VertexTransform.glsl"
#include "_VertexScreenPos.glsl"
#include "_Samplers.glsl"
#include "_GammaCorrection.glsl"

VERTEX_OUTPUT(vec2 vTexCoord)
VERTEX_OUTPUT(vec2 vScreenPos)

#ifdef URHO3D_PIXEL_SHADER

UNIFORM_BUFFER_BEGIN(6, Custom)
    UNIFORM(half3 cLuminanceWeights)
    UNIFORM(half cBloomThreshold)
    UNIFORM(vec2 cInputInvSize)
    UNIFORM(half2 cBloomMix)
UNIFORM_BUFFER_END(6, Custom)

#endif

#ifdef URHO3D_VERTEX_SHADER
void main()
{
    VertexTransform vertexTransform = GetVertexTransform();
    gl_Position = GetClipPos(vertexTransform.position.xyz);
    vTexCoord = GetQuadTexCoord(gl_Position);
    vScreenPos = GetScreenPosPreDiv(gl_Position);
}
#endif

#ifdef URHO3D_PIXEL_SHADER
void main()
{
#ifdef BRIGHT
    half3 inputColor = texture2D(sDiffMap, vTexCoord + vec2(-0.5, -0.5) * cInputInvSize).rgb;
    inputColor += texture2D(sDiffMap, vTexCoord + vec2(-0.5, 0.5) * cInputInvSize).rgb;
    inputColor += texture2D(sDiffMap, vTexCoord + vec2(0.5, 0.5) * cInputInvSize).rgb;
    inputColor += texture2D(sDiffMap, vTexCoord + vec2(0.5, -0.5) * cInputInvSize).rgb;
    inputColor *= 0.25;
    half brightness = dot(inputColor, cLuminanceWeights);
    gl_FragColor = vec4(inputColor * (brightness - cBloomThreshold), 1.0);
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
    half3 original = texture2D(sDiffMap, vScreenPos).rgb * cBloomMix.x;
    half3 bloom = texture2D(sNormalMap, vTexCoord).rgb  * cBloomMix.y;
    // Prevent oversaturation
    original *= max(vec3(1.0) - bloom, vec3(0.0));
    gl_FragColor = vec4(original + bloom, 1.0);
#endif
}
#endif

