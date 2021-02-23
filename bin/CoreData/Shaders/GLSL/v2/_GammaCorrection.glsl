#ifndef _GAMMA_CORRECTION_GLSL_
#define _GAMMA_CORRECTION_GLSL_

/// Convert color from gamma to linear space.
vec3 GammaToLinearSpace(vec3 color)
{
    return color * (color * (color * 0.305306011 + 0.682171111) + 0.012522878);
}

/// Convert color from gamma to linear space (with alpha).
vec4 GammaToLinearSpaceAlpha(vec4 color) { return vec4(GammaToLinearSpace(color.rgb), color.a); }

/// Convert color from linear to gamma space.
vec3 LinearToGammaSpace(vec3 color)
{
    const float p = 0.416666667;
    color = max(color, vec3(0.0, 0.0, 0.0));
    return max(1.055 * pow(color, vec3(p, p, p)) - 0.055, 0.0);
}

/// Convert color from linear to gamma space (with alpha).
vec4 LinearToGammaSpaceAlpha(vec4 color) { return vec4(LinearToGammaSpace(color.rgb), color.a); }

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

#define Texture_ToGamma_0(color) (color)
#define Texture_ToGamma_1(color) LinearToGammaSpaceAlpha(color)
#define Texture_ToLinear_0(color) GammaToLinearSpaceAlpha(color)
#define Texture_ToLinear_1(color) (color)

#ifdef URHO3D_GAMMA_CORRECTION
    #define Texture_ToLight_0(color) Texture_ToLinear_0(color)
    #define Texture_ToLight_1(color) Texture_ToLinear_1(color)
#else
    #define Texture_ToLight_0(color) Texture_ToGamma_0(color)
    #define Texture_ToLight_1(color) Texture_ToGamma_1(color)
#endif

/// @}

#endif // _GAMMA_CORRECTION_GLSL_
