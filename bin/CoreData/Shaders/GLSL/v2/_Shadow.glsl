#ifndef _SHADOW_GLSL_
#define _SHADOW_GLSL_

#ifndef _CONFIG_GLSL_
    #error Include _Config.glsl before _Shadow.glsl
#endif

#ifndef _SAMPLERS_GLSL_
    #error Include _Samplers.glsl before _Shadow.glsl
#endif

#ifdef URHO3D_HAS_SHADOW

#ifdef URHO3D_VERTEX_SHADER

// Convert vector from world to shadow space, for each cascade if applicable
void WorldSpaceToShadowCoord(out vec4 shadowPos[URHO3D_MAX_SHADOW_CASCADES], const vec4 worldPos)
{
    #if defined(URHO3D_LIGHT_DIRECTIONAL)
        #if URHO3D_MAX_SHADOW_CASCADES >= 4
            shadowPos[3] = worldPos * cLightMatrices[3];
        #endif
        #if URHO3D_MAX_SHADOW_CASCADES >= 3
            shadowPos[2] = worldPos * cLightMatrices[2];
        #endif
        #if URHO3D_MAX_SHADOW_CASCADES >= 2
            shadowPos[1] = worldPos * cLightMatrices[1];
        #endif
        shadowPos[0] = worldPos * cLightMatrices[0];
    #elif defined(URHO3D_LIGHT_POINT)
        shadowPos[0] = vec4(worldPos.xyz - cLightPos.xyz, 1.0);
    #elif defined(URHO3D_LIGHT_SPOT)
        shadowPos[0] = worldPos * cLightMatrices[0];
    #endif
}

#endif // URHO3D_VERTEX_SHADER

#ifdef URHO3D_PIXEL_SHADER

/// Convert 3D direction to UV coordinate of unwrapped 3x2 cubemap.
vec3 DirectionToUV(const vec3 vec, const vec2 bias)
{
    vec3 vecAbs = abs(vec);
    float invDominantAxis;
    vec2 uv;
    vec2 faceOffset;
    if (vecAbs.z >= vecAbs.x && vecAbs.z >= vecAbs.y)
    {
        faceOffset = vec.z < 0.0 ? vec2(2.0, 1.0) : vec2(1.0, 1.0);
        invDominantAxis = 1.0 / vecAbs.z;
        uv = vec2(vec.z < 0.0 ? -vec.x : vec.x, -vec.y);
    }
    else if (vecAbs.y >= vecAbs.x)
    {
        faceOffset = vec.y < 0.0 ? vec2(0.0, 1.0) : vec2(2.0, 0.0);
        invDominantAxis = 1.0 / vecAbs.y;
        uv = vec2(vec.x, vec.y < 0.0 ? -vec.z : vec.z);
    }
    else
    {
        faceOffset = vec.x < 0.0 ? vec2(1.0, 0.0) : vec2(0.0, 0.0);
        invDominantAxis = 1.0 / vecAbs.x;
        uv = vec2(vec.x < 0.0 ? vec.z : -vec.z, -vec.y);
    }
    uv *= bias;
    return vec3(uv * 0.5 * invDominantAxis + vec2(0.5, 0.5) + faceOffset, invDominantAxis);
}

#ifdef URHO3D_VARIANCE_SHADOW_MAP
    /// Calculate shadow value from sampled VSM texture and fragment depth
    half EvaluateVarianceShadow(const vec2 moments, const float depth)
    {
        float p = float(depth <= moments.x);
        float variance = moments.y - (moments.x * moments.x);

        variance = max(variance, cVSMShadowParams.x);
        float d = depth - moments.x;
        float pMax = variance / (variance + d*d);
        pMax = clamp((pMax - cVSMShadowParams.y) / (1.0 - cVSMShadowParams.y), 0.0, 1.0);

        return max(p, pMax);
    }
#else
    /// Sample shadow map texture at given 4-coordinate
    half SampleShadow(const vec4 shadowPos)
    {
        #if defined(GL3)
            return textureProj(sShadowMap, shadowPos);
        #elif defined(GL_ES)
            return texture2DProj(sShadowMap, shadowPos).r * shadowPos.w > shadowPos.z ? 1.0 : 0.0;
        #else
            return shadow2DProj(sShadowMap, shadowPos).r;
        #endif
    }

    /// Sample shadow map texture with given offset
    #if defined(GL3)
        #define SampleShadowOffset(shadowPos, identity, dx, dy) \
            textureProjOffset(sShadowMap, (shadowPos), ivec2(dx, dy))
    #else
        #define SampleShadowOffset(shadowPos, identity, dx, dy) \
            SampleShadow((shadowPos) + vec4(identity.x * float(dx), identity.y * float(dy), 0.0, 0.0))
    #endif

    /// Return UV coordinate offset corresponding to one texel
    #ifndef URHO3D_LIGHT_POINT
        #define GetShadowTexelOffset(shadowPos_w) (cShadowMapInvSize * shadowPos_w)
    #else
        #define GetShadowTexelOffset(shadowPos_w) cShadowMapInvSize
    #endif
#endif

/// Sample shadow map texture with predefined filtering at given 4-coordinate
half SampleShadowFiltered(const vec4 shadowPos)
{
    #if defined(URHO3D_VARIANCE_SHADOW_MAP)
        vec2 moments = texture2D(sShadowMap, shadowPos.xy / shadowPos.w).rg;
        return cShadowIntensity.y + cShadowIntensity.x * EvaluateVarianceShadow(moments, shadowPos.z / shadowPos.w);

    #elif URHO3D_SHADOW_PCF_SIZE == 2
        vec2 offsets = GetShadowTexelOffset(shadowPos.w);

        half4 shadowSamples;
        shadowSamples.x = SampleShadow(shadowPos);
        shadowSamples.y = SampleShadow(shadowPos + vec4(offsets.x, 0.0, 0.0, 0.0));
        shadowSamples.z = SampleShadow(shadowPos + vec4(0.0, offsets.y, 0.0, 0.0));
        shadowSamples.w = SampleShadow(shadowPos + vec4(offsets.xy, 0.0, 0.0));

        return cShadowIntensity.y + dot(cShadowIntensity.xxxx, shadowSamples);

    #elif URHO3D_SHADOW_PCF_SIZE == 3
        vec2 offsets = GetShadowTexelOffset(shadowPos.w);

        half sample4 = SampleShadow(shadowPos);

        half4 sample2;
        sample2.x = SampleShadowOffset(shadowPos, offsets, -1,  0);
        sample2.y = SampleShadowOffset(shadowPos, offsets,  1,  0);
        sample2.z = SampleShadowOffset(shadowPos, offsets,  0, -1);
        sample2.w = SampleShadowOffset(shadowPos, offsets,  0,  1);

        half4 sample1;
        sample1.x = SampleShadowOffset(shadowPos, offsets, -1, -1);
        sample1.y = SampleShadowOffset(shadowPos, offsets,  1, -1);
        sample1.z = SampleShadowOffset(shadowPos, offsets, -1,  1);
        sample1.w = SampleShadowOffset(shadowPos, offsets,  1,  1);

        const half3 factors = vec3(4.0 / 16.0, 2.0 / 16.0, 1.0 / 16.0);
        half average = sample4 * factors.x
            + dot(sample2, factors.yyyy)
            + dot(sample1, factors.zzzz);

        return cShadowIntensity.y + cShadowIntensity.x * average;

    #elif URHO3D_SHADOW_PCF_SIZE == 5
        vec2 offsets = GetShadowTexelOffset(shadowPos.w);

        half sample41 = SampleShadow(shadowPos);

        half4 sample26;
        sample26.x = SampleShadowOffset(shadowPos, offsets, -1,  0);
        sample26.y = SampleShadowOffset(shadowPos, offsets,  1,  0);
        sample26.z = SampleShadowOffset(shadowPos, offsets,  0, -1);
        sample26.w = SampleShadowOffset(shadowPos, offsets,  0,  1);

        half4 sample16;
        sample16.x = SampleShadowOffset(shadowPos, offsets, -1, -1);
        sample16.y = SampleShadowOffset(shadowPos, offsets,  1, -1);
        sample16.z = SampleShadowOffset(shadowPos, offsets, -1,  1);
        sample16.w = SampleShadowOffset(shadowPos, offsets,  1,  1);

        half4 sample7;
        sample7.x = SampleShadowOffset(shadowPos, offsets, -2,  0);
        sample7.y = SampleShadowOffset(shadowPos, offsets,  2,  0);
        sample7.z = SampleShadowOffset(shadowPos, offsets,  0, -2);
        sample7.w = SampleShadowOffset(shadowPos, offsets,  0,  2);

        half4 sample4_1;
        sample4_1.x = SampleShadowOffset(shadowPos, offsets, -2, -1);
        sample4_1.y = SampleShadowOffset(shadowPos, offsets, -1, -2);
        sample4_1.z = SampleShadowOffset(shadowPos, offsets,  2, -1);
        sample4_1.w = SampleShadowOffset(shadowPos, offsets,  1, -2);

        half4 sample4_2;
        sample4_2.x = SampleShadowOffset(shadowPos, offsets, -2, 1);
        sample4_2.y = SampleShadowOffset(shadowPos, offsets, -1, 2);
        sample4_2.z = SampleShadowOffset(shadowPos, offsets,  2, 1);
        sample4_2.w = SampleShadowOffset(shadowPos, offsets,  1, 2);

        const half4 factors = vec4(26.0 / 273.0, 16.0 / 273.0, 7.0 / 273.0, 4.0 / 273.0);
        half average = sample41 * (41.0 / 273.0)
            + dot(sample26, factors.xxxx)
            + dot(sample16, factors.yyyy)
            + dot(sample7, factors.zzzz)
            + dot(sample4_1, factors.wwww)
            + dot(sample4_2, factors.wwww);

        return cShadowIntensity.y + cShadowIntensity.x * average;

    #else
        return cShadowIntensity.y + cShadowIntensity.x * SampleShadow(shadowPos);
    #endif
}

/// Convert 4D-coordinate in shadow space to 4-UV used to sample shadow map
#if URHO3D_MAX_SHADOW_CASCADES == 4
    vec4 ShadowCoordToUV(const vec4 shadowPos[URHO3D_MAX_SHADOW_CASCADES], const float depth)
    {
        // If split contains depth, all further splits contain it too
        fixed4 splitSelector = vec4(lessThan(vec4(depth), cShadowSplits));
        // Subtract with offset to leave only one 1
        splitSelector.yzw = max(splitSelector.yzw - splitSelector.xyz, 0.0);
        return shadowPos[0] * splitSelector.x + shadowPos[1] * splitSelector.y
             + shadowPos[2] * splitSelector.z + shadowPos[3] * splitSelector.w;
    }
#elif defined(URHO3D_LIGHT_POINT)
    vec4 PointShadowCoordToUV(const vec3 shadowPos)
    {
        vec3 uvDepth = DirectionToUV(shadowPos, cShadowCubeUVBias.xy);
        uvDepth.xy = uvDepth.xy * cShadowCubeAdjust.xy + cShadowCubeAdjust.zw;
        uvDepth.z = cShadowDepthFade.x + cShadowDepthFade.y * uvDepth.z;
        return vec4(uvDepth, 1.0);
    }
    #define ShadowCoordToUV(shadowPos, depth) PointShadowCoordToUV(shadowPos[0].xyz)
#else
    #define ShadowCoordToUV(shadowPos, depth) (shadowPos[0])
#endif

/// Convert position in world space to shadow UV
#if defined(URHO3D_LIGHT_DIRECTIONAL) && URHO3D_MAX_SHADOW_CASCADES == 4
    vec4 WorldSpaceToShadowUV(const vec4 worldPos, const float depth)
    {
        if (depth < cShadowSplits.x)
            return worldPos * cLightMatrices[0];
        else if (depth < cShadowSplits.y)
            return worldPos * cLightMatrices[1];
        else if (depth < cShadowSplits.z)
            return worldPos * cLightMatrices[2];
        else
            return worldPos * cLightMatrices[3];
    }
#elif defined(URHO3D_LIGHT_DIRECTIONAL)
    #define WorldSpaceToShadowUV(worldPos, depth) ((worldPos) * cLightMatrices[0])
#elif defined(URHO3D_LIGHT_SPOT)
    #define WorldSpaceToShadowUV(worldPos, depth) ((worldPos) * cLightMatrices[0])
#elif defined(URHO3D_LIGHT_POINT)
    #define WorldSpaceToShadowUV(worldPos, depth) PointShadowCoordToUV((worldPos).xyz - cLightPos.xyz)
#endif

/// Fade shadow with distance
#if defined(URHO3D_LIGHT_DIRECTIONAL)
    half FadeShadow(half value, half depth)
    {
        return min(value + max((depth - cShadowDepthFade.z) * cShadowDepthFade.w, 0.0), 1.0);
    }
#else
    #define FadeShadow(value, depth) (value)
#endif

#endif // URHO3D_PIXEL_SHADER

#endif // URHO3D_HAS_SHADOW
#endif // _SHADOW_GLSL_
