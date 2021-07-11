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

/// Reinhard tone mapping
vec3 Reinhard(vec3 x)
{
    return x / (1.0 + x);
}

/// Reinhard tone mapping with white point
vec3 ReinhardWhite(vec3 x, float white)
{
    return x * (1.0 + x / white) / (1.0 + x);
}

/// Unchared2 tone mapping
vec3 Uncharted2(vec3 x)
{
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    return ((x*(A*x + C*B) + D*E) / (x*(A*x + B) + D*F)) - E/F;
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
    vec3 finalColor = texture2D(sDiffMap, vScreenPos).rgb;

#ifdef REINHARD
    finalColor = Reinhard(finalColor);
#endif
#ifdef REINHARDWHITE
    finalColor = ReinhardWhite(finalColor, 4.0);
#endif
#ifdef UNCHARTED2
    const vec3 whiteScale = 1.0 / Uncharted2(vec3(4.0));
    finalColor = Uncharted2(finalColor) * whiteScale;
#endif

    gl_FragColor = vec4(LinearToGammaSpace(finalColor), 1.0);
}
#endif
