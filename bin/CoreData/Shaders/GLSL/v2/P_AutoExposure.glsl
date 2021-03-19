#include "_Config.glsl"
#include "_Uniforms.glsl"
#include "_VertexLayout.glsl"
#include "_VertexTransform.glsl"
#include "_VertexScreenPos.glsl"
#include "_PixelOutput.glsl"
#include "_Samplers.glsl"
#include "_GammaCorrection.glsl"
#include "PostProcess.glsl"

VERTEX_OUTPUT(vec2 vTexCoord);
VERTEX_OUTPUT(vec2 vScreenPos);

#ifdef URHO3D_PIXEL_SHADER

UNIFORM_BUFFER_BEGIN(6, Custom)
    UNIFORM(half cAutoExposureAdaptRate)
    UNIFORM(half2 cAutoExposureLumRange)
    UNIFORM(half cAutoExposureMiddleGrey)
    UNIFORM(half2 cHDR128InvSize)
    UNIFORM(half2 cLum64InvSize)
    UNIFORM(half2 cLum16InvSize)
    UNIFORM(half2 cLum4InvSize)
UNIFORM_BUFFER_END()

float GatherAvgLum(sampler2D texSampler, vec2 texCoord, vec2 texelSize)
{
    float lumAvg = 0.0;
    lumAvg += texture2D(texSampler, texCoord + vec2(1.0, -1.0) * texelSize).r;
    lumAvg += texture2D(texSampler, texCoord + vec2(-1.0, 1.0) * texelSize).r;
    lumAvg += texture2D(texSampler, texCoord + vec2(1.0, 1.0) * texelSize).r;
    lumAvg += texture2D(texSampler, texCoord + vec2(1.0, -1.0) * texelSize).r;
    return lumAvg / 4.0;
}
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
    #ifdef LUMINANCE64
    float logLumSum = 0.0;
    logLumSum += log(dot(texture2D(sDiffMap, vTexCoord + vec2(-0.5, -0.5) * cHDR128InvSize).rgb, LumWeights) + 1e-5);
    logLumSum += log(dot(texture2D(sDiffMap, vTexCoord + vec2(-0.5, 0.5) * cHDR128InvSize).rgb, LumWeights) + 1e-5);
    logLumSum += log(dot(texture2D(sDiffMap, vTexCoord + vec2(0.5, 0.5) * cHDR128InvSize).rgb, LumWeights) + 1e-5);
    logLumSum += log(dot(texture2D(sDiffMap, vTexCoord + vec2(0.5, -0.5) * cHDR128InvSize).rgb, LumWeights) + 1e-5);
    gl_FragColor.r = logLumSum * 0.25;
    #endif

    #ifdef LUMINANCE16
    gl_FragColor.r = GatherAvgLum(sDiffMap, vTexCoord, cLum64InvSize);
    #endif

    #ifdef LUMINANCE4
    gl_FragColor.r = GatherAvgLum(sDiffMap, vTexCoord, cLum16InvSize);
    #endif

    #ifdef LUMINANCE1
    gl_FragColor.r = exp(GatherAvgLum(sDiffMap, vTexCoord, cLum4InvSize) / 16.0);
    #endif

    #ifdef ADAPTLUMINANCE
    float adaptedLum = texture2D(sDiffMap, vTexCoord).r;
    float lum = clamp(texture2D(sNormalMap, vTexCoord).r, cAutoExposureLumRange.x, cAutoExposureLumRange.y);
    gl_FragColor.r = adaptedLum + (lum - adaptedLum) * (1.0 - exp(-cDeltaTime * cAutoExposureAdaptRate));
    #endif

    #ifdef EXPOSE
    vec3 color = texture2D(sDiffMap, vScreenPos).rgb;
    #ifdef GAMMA
        color = GammaToLinearSpace(color);
    #endif
    float adaptedLum = texture2D(sNormalMap, vTexCoord).r;
    vec3 finalColor = color * (cAutoExposureMiddleGrey / adaptedLum);
    #ifdef GAMMA
        finalColor = LinearToGammaSpace(finalColor);
    #endif
    gl_FragColor = vec4(finalColor, 1.0);
    #endif
}
#endif
