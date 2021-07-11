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
    UNIFORM(vec2 cInputInvSize)
    UNIFORM(vec2 cMinMaxExposure)
    UNIFORM(float cAdaptRate)
UNIFORM_BUFFER_END(6, Custom)

float GatherAvgLum(sampler2D texSampler, vec2 texCoord)
{
    float lumAvg = 0.0;
    lumAvg += texture2D(texSampler, texCoord + vec2(1.0, -1.0) * cInputInvSize).r;
    lumAvg += texture2D(texSampler, texCoord + vec2(-1.0, 1.0) * cInputInvSize).r;
    lumAvg += texture2D(texSampler, texCoord + vec2(1.0, 1.0) * cInputInvSize).r;
    lumAvg += texture2D(texSampler, texCoord + vec2(1.0, -1.0) * cInputInvSize).r;
    return lumAvg / 4.0;
}

float GetEV100(float luminance)
{
    return log2(luminance * (100.0 / 12.5));
}

float GetExposure(float ev100)
{
    return 1.0 / (pow(2.0, ev100) * 1.2);
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
#ifdef LUMINANCE64
    const vec3 LumWeights = vec3(0.2126, 0.7152, 0.0722);
    float logLumSum = 0.0;
    logLumSum += log(dot(texture2D(sDiffMap, vTexCoord + vec2(-0.5, -0.5) * cInputInvSize).rgb, LumWeights) + 1e-4);
    logLumSum += log(dot(texture2D(sDiffMap, vTexCoord + vec2(-0.5, 0.5) * cInputInvSize).rgb, LumWeights) + 1e-4);
    logLumSum += log(dot(texture2D(sDiffMap, vTexCoord + vec2(0.5, 0.5) * cInputInvSize).rgb, LumWeights) + 1e-4);
    logLumSum += log(dot(texture2D(sDiffMap, vTexCoord + vec2(0.5, -0.5) * cInputInvSize).rgb, LumWeights) + 1e-4);
    gl_FragColor.r = logLumSum * 0.25;
#endif

#ifdef LUMINANCE16
    gl_FragColor.r = GatherAvgLum(sDiffMap, vTexCoord);
#endif

#ifdef LUMINANCE4
    gl_FragColor.r = GatherAvgLum(sDiffMap, vTexCoord);
#endif

#ifdef LUMINANCE1
    gl_FragColor.r = exp(GatherAvgLum(sDiffMap, vTexCoord));
#endif

#ifdef ADAPTLUMINANCE
    float previousLuminance = texture2D(sDiffMap, vTexCoord).r;
    float currentLuminance = texture2D(sNormalMap, vTexCoord).r;
    gl_FragColor.r = previousLuminance + (currentLuminance - previousLuminance) * (1.0 - exp(-cDeltaTime * cAdaptRate));
#endif

#ifdef EXPOSURE
    vec3 color = texture2D(sDiffMap, vScreenPos).rgb;

    #ifdef AUTOEXPOSURE
        float luminance = texture2D(sNormalMap, vTexCoord).r;
        float ev100 = GetEV100(luminance);
        float exposure = clamp(GetExposure(ev100), cMinMaxExposure.x, cMinMaxExposure.y);
    #else
        float exposure = cMinMaxExposure.x;
    #endif

    const float whitePoint = 4.0;
    gl_FragColor = vec4(min(color * exposure, vec3(whitePoint, whitePoint, whitePoint)), 1.0);
#endif
}
#endif
