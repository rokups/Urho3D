/// _GammaCorrection.glsl
/// Utilities used to convert from light to gamma color space and vice versa.
/// Utilities to convert from/to light space configured by URHO3D_GAMMA_CORRECTION.
/// Utilities to sample texture contents based on texture properties.
#ifndef _GAMMA_CORRECTION_GLSL_
#define _GAMMA_CORRECTION_GLSL_

#ifndef _CONFIG_GLSL_
    #error Include _Config.glsl before _GammaCorrection.glsl
#endif

/// Convert color from gamma to linear space.
half3 GammaToLinearSpace(const half3 color)
{
    return color * (color * (color * 0.305306011 + 0.682171111) + 0.012522878);
}

/// Convert color from gamma to linear space (with alpha).
half4 GammaToLinearSpaceAlpha(const half4 color) { return vec4(GammaToLinearSpace(color.rgb), color.a); }

/// Convert color from linear to gamma space.
half3 LinearToGammaSpace(half3 color)
{
    const float p = 0.416666667;
    color = max(color, vec3(0.0, 0.0, 0.0));
    return max(1.055 * pow(color, vec3(p, p, p)) - 0.055, 0.0);
}

/// Convert color from linear to gamma space (with alpha).
half4 LinearToGammaSpaceAlpha(const half4 color) { return vec4(LinearToGammaSpace(color.rgb), color.a); }

/// Convert from and to light space.
#ifdef URHO3D_GAMMA_CORRECTION
    #define GammaToLightSpace(color) GammaToLinearSpace(color)
    #define GammaToLightSpaceAlpha(color) GammaToLinearSpaceAlpha(color)

    #define LightToGammaSpace(color) LinearToGammaSpace(color)
    #define LightToGammaSpaceAlpha(color) LinearToGammaSpaceAlpha(color)

    #define LinearToLightSpace(color) (color)
    #define LinearToLightSpaceAlpha(color) (color)

    #define LightToLinearSpace(color) (color)
    #define LightToLinearSpaceAlpha(color) (color)
#else
    #define GammaToLightSpace(color) (color)
    #define GammaToLightSpaceAlpha(color) (color)

    #define LightToGammaSpace(color) (color)
    #define LightToGammaSpaceAlpha(color) (color)

    #define LinearToLightSpace(color) LinearToGammaSpace(color)
    #define LinearToLightSpaceAlpha(color) LinearToGammaSpaceAlpha(color)

    #define LightToLinearSpace(color) GammaToLinearSpace(color)
    #define LightToLinearSpaceAlpha(color) GammaToLinearSpaceAlpha(color)
#endif

/// Utilities to read textures in given color space. Suffix is texture mode hint that equals to min(isLinear + sRGB, 1).
/// @{
#define Texture_ToGammaAlpha_0(color) (color)
#define Texture_ToGammaAlpha_1(color) LinearToGammaSpaceAlpha(color)
#define Texture_ToLinearAlpha_0(color) GammaToLinearSpaceAlpha(color)
#define Texture_ToLinearAlpha_1(color) (color)

#define Texture_ToGamma_0(color) (color)
#define Texture_ToGamma_1(color) LinearToGammaSpace(color)
#define Texture_ToLinear_0(color) GammaToLinearSpace(color)
#define Texture_ToLinear_1(color) (color)

#ifdef URHO3D_GAMMA_CORRECTION
    #define Texture_ToLightAlpha_0(color) Texture_ToLinearAlpha_0(color)
    #define Texture_ToLightAlpha_1(color) Texture_ToLinearAlpha_1(color)
    #define Texture_ToLight_0(color) Texture_ToLinear_0(color)
    #define Texture_ToLight_1(color) Texture_ToLinear_1(color)
#else
    #define Texture_ToLightAlpha_0(color) Texture_ToGammaAlpha_0(color)
    #define Texture_ToLightAlpha_1(color) Texture_ToGammaAlpha_1(color)
    #define Texture_ToLight_0(color) Texture_ToGamma_0(color)
    #define Texture_ToLight_1(color) Texture_ToGamma_1(color)
#endif
/// @}

#endif // _GAMMA_CORRECTION_GLSL_
