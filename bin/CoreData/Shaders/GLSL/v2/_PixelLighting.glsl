#ifndef _PIXEL_LIGHTING_GLSL_
#define _PIXEL_LIGHTING_GLSL_

#ifndef _UNIFORMS_GLSL_
    #error Include "_Uniforms.glsl" before "_PixelLighting.glsl"
#endif

/// Return light vector normalized to light range.
#ifdef URHO3D_LIGHT_DIRECTIONAL
    #define GetLightVector(worldPos) cLightDir
#else
    #define GetLightVector(worldPos) ((cLightPos.xyz - (worldPos)) * cLightPos.w)
#endif

/// Return eye vector, not normalized.
#define GetEyeVector(worldPos) (cCameraPos - (worldPos))

#ifdef URHO3D_PIXEL_SHADER
    /// Normalize light vector, return normalized vector and distance
    #ifdef URHO3D_LIGHT_DIRECTIONAL
        #define NormalizeLightVector(lightVec) vec4(lightVec, 1.0)
    #else
        vec4 NormalizeLightVector(vec3 lightVec)
        {
            // TODO(renderer): Revisit epsilon
            float lightDist = max(0.001, length(lightVec));
            return vec4(lightVec / lightDist, lightDist);
        }
    #endif

    /// Return light ramp value.
    #ifdef URHO3D_LIGHT_DIRECTIONAL
        #define GetLightDistanceAttenuation(normalizedDistance) 1.0
    #elif defined(URHO3D_LIGHT_CUSTOM_RAMP)
        #define GetLightDistanceAttenuation(normalizedDistance) texture2D(sLightRampMap, vec2((normalizedDistance), 0.0)).r
    #else
        float GetLightDistanceAttenuation(float normalizedDistance)
        {
            float invDistance = max(0.0, 1.0 - normalizedDistance);
            return invDistance * invDistance;
        }
    #endif

    /// Return light color at given direction.
    #ifdef URHO3D_LIGHT_CUSTOM_SHAPE
        #if defined(URHO3D_LIGHT_SPOT)
            vec3 GetLightColorFromShape(vec4 shapePos)
            {
                vec2 c = vec2(0.0, shapePos.w);
                bvec3 binEnough = greaterThanEqual(shapePos.xyw, c.xxx);
                bvec2 smallEnough = lessThanEqual(shapePos.xy, c.yy);
                return all(binEnough) && all(smallEnough)
                    ? texture2DProj(sLightSpotMap, shapePos).rgb * cLightColor.rgb
                    : vec3(0.0, 0.0, 0.0);
            }
        #elif defined(URHO3D_LIGHT_POINT)
            vec3 GetLightColorFromShape(vec4 shapePos)
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

    /// Evaluate diffuse lighting intensity.
    #define GetDiffuseIntensity(normal, lightVec, normalizedDistance) \
        (CONVERT_N_DOT_L(dot(normal, lightVec)) * GetLightDistanceAttenuation(normalizedDistance))

    /// Evaluate specular lighting intensity.
    float GetSpecularIntensity(vec3 normal, vec3 eyeVec, vec3 lightVec, float specularPower)
    {
        vec3 halfVec = normalize(eyeVec + lightVec);
        return pow(max(dot(normal, halfVec), 0.0), specularPower);
    }

#endif // URHO3D_PIXEL_SHADER

#endif // _PIXEL_LIGHTING_GLSL_
