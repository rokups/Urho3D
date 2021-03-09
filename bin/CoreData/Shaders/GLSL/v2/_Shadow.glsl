#ifndef _SHADOW_GLSL_
#define _SHADOW_GLSL_

#ifndef _CONFIG_GLSL_
    #error Include "_Config.glsl" before "_Shadow.glsl"
#endif

#ifndef _SAMPLERS_GLSL_
    #error Include "_Samplers.glsl" before "_Shadow.glsl"
#endif

#ifdef URHO3D_HAS_SHADOW

#ifdef URHO3D_VERTEX_SHADER

// Convert vector from world to shadow space, for each cascade if applicable
void WorldSpaceToShadowCoord(out vec4 shadowPos[URHO3D_SHADOW_NUM_CASCADES], vec4 worldPos)
{
    #if defined(URHO3D_LIGHT_DIRECTIONAL)
        for (int i = 0; i < URHO3D_SHADOW_NUM_CASCADES; i++)
            shadowPos[i] = worldPos * cLightMatrices[i];
    #elif defined(URHO3D_LIGHT_POINT)
        shadowPos[0] = vec4(worldPos.xyz - cLightPos.xyz, 1.0);
    #elif defined(URHO3D_LIGHT_SPOT)
        shadowPos[0] = worldPos * cLightMatrices[1];
    #endif
}

#endif // URHO3D_VERTEX_SHADER

#ifdef URHO3D_PIXEL_SHADER

/// Convert 3D direction to UV coordinate of unwrapped 3x2 cubemap.
vec3 DirectionToUV(vec3 vec, vec2 bias)
{
    vec3 vecAbs = abs(vec);
    float invDominantAxis;
    vec2 uv;
    vec2 faceOffset;
    if(vecAbs.z >= vecAbs.x && vecAbs.z >= vecAbs.y)
    {
        faceOffset = vec.z < 0.0 ? vec2(2.0, 1.0) : vec2(1.0, 1.0);
        invDominantAxis = 1.0 / vecAbs.z;
        uv = vec2(vec.z < 0.0 ? -vec.x : vec.x, -vec.y);
    }
    else if(vecAbs.y >= vecAbs.x)
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
    float EvaluateVarianceShadow(optional_highp vec2 moments, float depth)
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
    float SampleShadow(optional_highp vec4 shadowPos)
    {
        #if defined(GL3)
            return textureProj(sShadowMap, shadowPos);
        #elif defined(GL_ES)
            return texture2DProj(sShadowMap, shadowPos).r * shadowPos.w > shadowPos.z;
        #else
            return shadow2DProj(sShadowMap, shadowPos).r;
        #endif
    }

    /// Return UV coordinate offset corresponding to one texel
    #ifndef URHO3D_LIGHT_POINT
        #define GetShadowTexelOffset(shadowPos_w) (cShadowMapInvSize * shadowPos_w)
    #else
        #define GetShadowTexelOffset(shadowPos_w) cShadowMapInvSize
    #endif
#endif

/// Sample shadow map texture with predefined filtering at given 4-coordinate
float SampleShadowFiltered(optional_highp vec4 shadowPos)
{
    #if defined(URHO3D_VARIANCE_SHADOW_MAP)
        optional_highp vec2 moments = texture2D(sShadowMap, shadowPos.xy / shadowPos.w).rg;
        return cShadowIntensity.y + cShadowIntensity.x * EvaluateVarianceShadow(moments, shadowPos.z / shadowPos.w);
    #elif URHO3D_SHADOW_PCF_SIZE <= 1 || URHO3D_SHADOW_PCF_SIZE > 2
        return cShadowIntensity.y + cShadowIntensity.x * SampleShadow(shadowPos);
    #elif URHO3D_SHADOW_PCF_SIZE == 2
        vec2 offsets = GetShadowTexelOffset(shadowPos.w);
        vec4 shadowSamples;
        shadowSamples.x = SampleShadow(shadowPos);
        shadowSamples.y = SampleShadow(shadowPos + vec4(offsets.x, 0.0, 0.0, 0.0));
        shadowSamples.z = SampleShadow(shadowPos + vec4(0.0, offsets.y, 0.0, 0.0));
        shadowSamples.w = SampleShadow(shadowPos + vec4(offsets.xy, 0.0, 0.0));
        return cShadowIntensity.y + dot(cShadowIntensity.xxxx, shadowSamples);
    #endif
}

/// Convert 4D-coordinate in shadow space to 4-UV used to sample shadow map
#if URHO3D_SHADOW_NUM_CASCADES == 4
    optional_highp vec4 ShadowCoordToUV(optional_highp vec4 shadowPos[URHO3D_SHADOW_NUM_CASCADES], float depth)
    {
        // TODO(renderer): Optimize this?
        if (depth < cShadowSplits.x)
            return shadowPos[0];
        else if (depth < cShadowSplits.y)
            return shadowPos[1];
        else if (depth < cShadowSplits.z)
            return shadowPos[2];
        else
            return shadowPos[3];
    }
#elif defined(URHO3D_LIGHT_POINT)
    optional_highp vec4 PointShadowCoordToUV(optional_highp vec3 shadowPos)
    {
        optional_highp vec3 uvDepth = DirectionToUV(shadowPos, cShadowCubeUVBias.xy);
        uvDepth.xy = uvDepth.xy * cShadowCubeAdjust.xy + cShadowCubeAdjust.zw;
        uvDepth.z = cShadowDepthFade.x + cShadowDepthFade.y * uvDepth.z;
        return vec4(uvDepth, 1.0);
    }
    #define ShadowCoordToUV(shadowPos, depth) PointShadowCoordToUV(shadowPos[0].xyz)
#else
    #define ShadowCoordToUV(shadowPos, depth) (shadowPos[0])
#endif

/// Convert position in world space to shadow UV
#if defined(URHO3D_LIGHT_DIRECTIONAL) && URHO3D_SHADOW_NUM_CASCADES == 4
    optional_highp vec4 WorldSpaceToShadowUV(optional_highp vec4 worldPos, float depth)
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
    #define WorldSpaceToShadowUV(worldPos, depth) ((worldPos) * cLightMatrices[1])
#elif defined(URHO3D_LIGHT_POINT)
    #define WorldSpaceToShadowUV(worldPos, depth) PointShadowCoordToUV((worldPos).xyz - cLightPos.xyz)
#endif

/// Fade shadow with distance
#if defined(URHO3D_LIGHT_DIRECTIONAL)
    float FadeShadow(float value, float depth)
    {
        return min(value + max((depth - cShadowDepthFade.z) * cShadowDepthFade.w, 0.0), 1.0);
    }
#else
    #define FadeShadow(value, depth) (value)
#endif

/// Calculate effective shadow factor (forward lighting).
float GetForwardShadow(optional_highp vec4 shadowPos[URHO3D_SHADOW_NUM_CASCADES], float depth)
{
    return FadeShadow(SampleShadowFiltered(ShadowCoordToUV(shadowPos, depth)), depth);
}

/// Calculate effective shadow factor (deferred lighting).
float GetDeferredShadow(vec4 worldPos, float depth)
{
    return FadeShadow(SampleShadowFiltered(WorldSpaceToShadowUV(worldPos, depth)), depth);
}

#endif // URHO3D_PIXEL_SHADER

#endif // URHO3D_HAS_SHADOW
#endif // _SHADOW_GLSL_
