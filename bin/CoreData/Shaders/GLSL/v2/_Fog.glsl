#ifndef _FOG_GLSL_
#define _FOG_GLSL_

#ifndef _UNIFORMS_GLSL_
    #error Include _Uniforms.glsl before _Fog.glsl
#endif

/// Evaluate fog factor for given linear depth. 0 is pure fog, 1 is pure color.
fixed GetFogFactor(const half depth)
{
    return clamp((cFogParams.x - depth) * cFogParams.y, 0.0, 1.0);
}

#ifdef URHO3D_PIXEL_SHADER
    #ifndef URHO3D_ADDITIVE_LIGHT_PASS
        half3 ApplyFog(const half3 color, const fixed fogFactor)
        {
            return mix(cFogColor, color, fogFactor);
        }
    #else
        half3 ApplyFog(const half3 color, const fixed fogFactor)
        {
            return color * fogFactor;
        }
    #endif
#endif // URHO3D_PIXEL_SHADER

#endif // _FOG_GLSL_
