#ifndef _DIRECT_LIGHTING_GLSL_
#define _DIRECT_LIGHTING_GLSL_

#ifndef _UNIFORMS_GLSL_
    #error Include _Uniforms.glsl before _DirectLighting.glsl
#endif

#ifdef URHO3D_LIGHT_PASS

/// Return light vector normalized to light range.
#ifdef URHO3D_LIGHT_DIRECTIONAL
    #define GetLightVector(worldPos) cLightDir
#else
    #define GetLightVector(worldPos) ((cLightPos.xyz - (worldPos)) * cLightPos.w)
#endif

#ifdef URHO3D_PIXEL_SHADER
    /// Pixel light input independent of surface.
    struct DirectLightData
    {
        /// Light color, with optional shape texture applied.
        half3 lightColor;
        /// Normalized light vector. w component is normalized distance to light.
        half4 lightVec;
    #ifdef URHO3D_HAS_SHADOW
        /// Shadow factor. 0 is fully shadowed.
        fixed shadow;
    #endif
    };

    /// Normalize light vector, return normalized vector and distance
    #ifdef URHO3D_LIGHT_DIRECTIONAL
        #define NormalizeLightVector(lightVec) vec4(lightVec, 0.0)
    #else
        half4 NormalizeLightVector(const half3 lightVec)
        {
            half lightDist = max(0.001, length(lightVec));
            return vec4(lightVec / lightDist, lightDist);
        }
    #endif

    /// Return light ramp value.
    #ifdef URHO3D_LIGHT_DIRECTIONAL
        #define GetLightDistanceAttenuation(normalizedDistance) 1.0
    #elif defined(URHO3D_LIGHT_CUSTOM_RAMP)
        #define GetLightDistanceAttenuation(normalizedDistance) texture2D(sLightRampMap, vec2((normalizedDistance), 0.0)).r
    #else
        half GetLightDistanceAttenuation(const half normalizedDistance)
        {
            half invDistance = max(0.0, 1.0 - normalizedDistance);
            return invDistance * invDistance;
        }
    #endif

    /// Return light color at given direction.
    #ifdef URHO3D_LIGHT_CUSTOM_SHAPE
        #if defined(URHO3D_LIGHT_SPOT)
            half3 GetLightColorFromShape(const vec4 shapePos)
            {
                vec2 c = vec2(0.0, shapePos.w);
                bvec3 binEnough = greaterThanEqual(shapePos.xyw, c.xxx);
                bvec2 smallEnough = lessThanEqual(shapePos.xy, c.yy);
                return all(binEnough) && all(smallEnough)
                    ? texture2DProj(sLightSpotMap, shapePos).rgb * cLightColor.rgb
                    : vec3(0.0, 0.0, 0.0);
            }
        #elif defined(URHO3D_LIGHT_POINT)
            half3 GetLightColorFromShape(const vec4 shapePos)
            {
                return textureCube(sLightCubeMap, shapePos.xyz).rgb * cLightColor.rgb;
            }
        #elif defined(URHO3D_LIGHT_DIRECTIONALPOINT)
            #define GetLightColorFromShape(shapePos) (cLightColor.rgb)
        #endif
    #else
        #ifdef URHO3D_LIGHT_SPOT
            #define GetLightColor(lightVec) \
                (cLightColor.rgb * clamp((dot(lightVec, cLightDir) - cSpotAngle.x) * cSpotAngle.y, 0.0, 1.0))
        #else
            #define GetLightColor(lightVec) (cLightColor.rgb)
        #endif
    #endif

    /// Calculate light attenuation. Includes shadow and distance attenuation.
    half GetDirectLightAttenuation(const DirectLightData lightData)
    {
        half attenuation = GetLightDistanceAttenuation(lightData.lightVec.w);
    #ifdef URHO3D_HAS_SHADOW
        attenuation *= lightData.shadow;
    #endif
        return attenuation;
    }

#endif // URHO3D_PIXEL_SHADER
#endif // URHO3D_LIGHT_PASS

#endif // _DIRECT_LIGHTING_GLSL_
