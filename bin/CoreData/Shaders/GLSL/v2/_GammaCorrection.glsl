/// Convert color from gamma to linear space.
vec3 GammaToLinearSpace(vec3 color)
{
    return color * (color * (color * 0.305306011 + 0.682171111) + 0.012522878);
}

/// Convert color from gamma to linear space.
vec3 LinearToGammaSpace(vec3 color)
{
    const float p = 0.416666667;
    color = max(color, vec3(0.0, 0.0, 0.0));
    return max(1.055 * pow(color, vec3(p, p, p)) - 0.055, 0.0);
}

/// Helper utilities to convert between color spaces.
/// @{

/// Convert from gamma to linear space.
#define Color_GammaToLinear3(color) GammaToLinearSpace(color)
vec4 Color_GammaToLinear4(vec4 color) { return vec4(GammaToLinearSpace(color.rgb), color.a); }
#define COLOR_GAMMATOLINEAR4(color) Color_GammaToLinear4(color)

/// Keep color space (no conversion).
#define Color_Noop(color) (color)
#define COLOR_NOOP(color) (color)

/// Convert from linear to gamma space.
#define Color_LinearToGamma3(color) LinearToGammaSpace(color)
vec4 Color_LinearToGamma4(vec4 color) { return vec4(LinearToGammaSpace(color.rgb), color.a); }
#define COLOR_LINEARTOGAMMA4(color) Color_LinearToGamma4(color)

/// Convert from linear to gamma space twice. This is fallback for misused sRGB textures.
#define Color_LinearToGammaSquare3(color) LinearToGammaSpace(LinearToGammaSpace(color))
vec4 Color_LinearToGammaSquare4(vec4 color) { return vec4(LinearToGammaSpace(LinearToGammaSpace(color.rgb)), color.a); }
#define COLOR_LINEARTOGAMMASQUARE4(color) Color_LinearToGammaSquare4(color)

/// Convert from/to light space.
#ifdef URHO3D_GAMMA_CORRECTION
    #define Color_GammaToLight3(color) Color_GammaToLinear3(color)
    #define Color_LightToGamma3(color) Color_LinearToGamma3(color)
    #define Color_GammaToLight4(color) Color_GammaToLinear4(color)
    #define Color_LightToGamma4(color) Color_LinearToGamma4(color)
#else
    #define Color_GammaToLight3(color) (color)
    #define Color_LightToGamma3(color) (color)
    #define Color_GammaToLight4(color) (color)
    #define Color_LightToGamma4(color) (color)
#endif
/// @}
