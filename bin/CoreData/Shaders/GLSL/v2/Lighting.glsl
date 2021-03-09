#ifdef COMPILEVS

#ifdef SHADOW

#if defined(DIRLIGHT) && (!defined(GL_ES) || defined(WEBGL))
    #define NUMCASCADES 4
#else
    #define NUMCASCADES 1
#endif

vec4 GetShadowPos(int index, vec3 normal, vec4 projWorldPos)
{
    /*#ifdef NORMALOFFSET
        float normalOffsetScale[4];
        normalOffsetScale[0] = cNormalOffsetScale.x;
        normalOffsetScale[1] = cNormalOffsetScale.y;
        normalOffsetScale[2] = cNormalOffsetScale.z;
        normalOffsetScale[3] = cNormalOffsetScale.w;

        #ifdef DIRLIGHT
            float cosAngle = clamp(1.0 - dot(normal, cLightDir), 0.0, 1.0);
        #else
            float cosAngle = clamp(1.0 - dot(normal, normalize(cLightPos.xyz - projWorldPos.xyz)), 0.0, 1.0);
        #endif
        projWorldPos.xyz += cosAngle * normalOffsetScale[index] * normal;
    #endif*/

    #if defined(DIRLIGHT)
        return projWorldPos * cLightMatrices[index];
    #elif defined(SPOTLIGHT)
        return projWorldPos * cLightMatrices[1];
    #else
        return vec4(projWorldPos.xyz - cLightPos.xyz, 1.0);
    #endif
}

#endif
#endif

#ifdef COMPILEPS
float GetDiffuse(vec3 normal, vec3 worldPos, out vec3 lightDir)
{
    #ifdef DIRLIGHT
        lightDir = cLightDir;
        #ifdef TRANSLUCENT
            return abs(dot(normal, lightDir));
        #else
            return max(dot(normal, lightDir), 0.0);
        #endif
    #else
        vec3 lightVec = (cLightPos.xyz - worldPos) * cLightPos.w;
        float lightDist = length(lightVec);
        lightDir = lightVec / lightDist;
        #ifdef TRANSLUCENT
            return abs(dot(normal, lightDir)) * texture2D(sLightRampMap, vec2(lightDist, 0.0)).r;
        #else
            return max(dot(normal, lightDir), 0.0) * texture2D(sLightRampMap, vec2(lightDist, 0.0)).r;
        #endif
    #endif
}

float GetAtten(vec3 normal, vec3 worldPos, out vec3 lightDir)
{
    lightDir = cLightDir;
    return clamp(dot(normal, lightDir), 0.0, 1.0);
}

float GetAttenPoint(vec3 normal, vec3 worldPos, out vec3 lightDir)
{
    vec3 lightVec = (cLightPos.xyz - worldPos) * cLightPos.w;
    float lightDist = length(lightVec);
    float falloff = pow(clamp(1.0 - pow(lightDist / 1.0, 4.0), 0.0, 1.0), 2.0) * 3.14159265358979323846 / (4.0 * 3.14159265358979323846)*(pow(lightDist, 2.0) + 1.0);
    lightDir = lightVec / lightDist;
    return clamp(dot(normal, lightDir), 0.0, 1.0) * falloff;

}

float GetAttenSpot(vec3 normal, vec3 worldPos, out vec3 lightDir)
{
    vec3 lightVec = (cLightPos.xyz - worldPos) * cLightPos.w;
    float lightDist = length(lightVec);
    float falloff = pow(clamp(1.0 - pow(lightDist / 1.0, 4.0), 0.0, 1.0), 2.0) / (pow(lightDist, 2.0) + 1.0);

    lightDir = lightVec / lightDist;
    return clamp(dot(normal, lightDir), 0.0, 1.0) * falloff;

}

float GetDiffuseVolumetric(vec3 worldPos)
{
    #ifdef DIRLIGHT
        return 1.0;
    #else
        vec3 lightVec = (cLightPos.xyz - worldPos) * cLightPos.w;
        float lightDist = length(lightVec);
        return texture2D(sLightRampMap, vec2(lightDist, 0.0)).r;
    #endif
}

float GetSpecular(vec3 normal, vec3 eyeVec, vec3 lightDir, float specularPower)
{
    vec3 halfVec = normalize(normalize(eyeVec) + lightDir);
    return pow(max(dot(normal, halfVec), 0.0), specularPower);
}

float GetIntensity(vec3 color)
{
    return dot(color, vec3(0.299, 0.587, 0.114));
}

#ifdef SHADOW

#if defined(DIRLIGHT) && (!defined(GL_ES) || defined(WEBGL))
    #define NUMCASCADES 4
#else
    #define NUMCASCADES 1
#endif

#ifdef VSM_SHADOW
float ReduceLightBleeding(float min, float p_max)
{
    return clamp((p_max - min) / (1.0 - min), 0.0, 1.0);
}

float Chebyshev(vec2 Moments, float depth)
{
    //One-tailed inequality valid if depth > Moments.x
    float p = float(depth <= Moments.x);
    //Compute variance.
    float Variance = Moments.y - (Moments.x * Moments.x);

    float minVariance = cVSMShadowParams.x;
    Variance = max(Variance, minVariance);
    //Compute probabilistic upper bound.
    float d = depth - Moments.x;
    float p_max = Variance / (Variance + d*d);
    // Prevent light bleeding
    p_max = ReduceLightBleeding(cVSMShadowParams.y, p_max);

    return max(p, p_max);
}
#endif

#ifndef GL_ES
float GetShadow(vec4 shadowPos)
{
    #if defined(SIMPLE_SHADOW)
        // Take one sample
        #ifndef GL3
            float inLight = shadow2DProj(sShadowMap, shadowPos).r;
        #else
            float inLight = textureProj(sShadowMap, shadowPos);
        #endif
        return cShadowIntensity.y + cShadowIntensity.x * inLight;
    #elif defined(PCF_SHADOW)
        // Take four samples and average them
        // Note: in case of sampling a point light cube shadow, we optimize out the w divide as it has already been performed
        #ifndef POINTLIGHT
            vec2 offsets = cShadowMapInvSize * shadowPos.w;
        #else
            vec2 offsets = cShadowMapInvSize;
        #endif
        #ifndef GL3
            return cShadowIntensity.y + cShadowIntensity.x * (shadow2DProj(sShadowMap, shadowPos).r +
                shadow2DProj(sShadowMap, vec4(shadowPos.x + offsets.x, shadowPos.yzw)).r +
                shadow2DProj(sShadowMap, vec4(shadowPos.x, shadowPos.y + offsets.y, shadowPos.zw)).r +
                shadow2DProj(sShadowMap, vec4(shadowPos.xy + offsets.xy, shadowPos.zw)).r);
        #else
            return cShadowIntensity.y + cShadowIntensity.x * (textureProj(sShadowMap, shadowPos) +
                textureProj(sShadowMap, vec4(shadowPos.x + offsets.x, shadowPos.yzw)) +
                textureProj(sShadowMap, vec4(shadowPos.x, shadowPos.y + offsets.y, shadowPos.zw)) +
                textureProj(sShadowMap, vec4(shadowPos.xy + offsets.xy, shadowPos.zw)));
        #endif
    #elif defined(VSM_SHADOW)
        vec2 samples = texture2D(sShadowMap, shadowPos.xy / shadowPos.w).rg;
        return cShadowIntensity.y + cShadowIntensity.x * Chebyshev(samples, shadowPos.z / shadowPos.w);
    #endif
}
#else
float GetShadow(highp vec4 shadowPos)
{
    #if defined(SIMPLE_SHADOW)
        // Take one sample
        return cShadowIntensity.y + (texture2DProj(sShadowMap, shadowPos).r * shadowPos.w > shadowPos.z ? cShadowIntensity.x : 0.0);
    #elif defined(PCF_SHADOW)
        // Take four samples and average them
        vec2 offsets = cShadowMapInvSize * shadowPos.w;
        vec4 inLight = vec4(
            texture2DProj(sShadowMap, shadowPos).r * shadowPos.w > shadowPos.z,
            texture2DProj(sShadowMap, vec4(shadowPos.x + offsets.x, shadowPos.yzw)).r * shadowPos.w > shadowPos.z,
            texture2DProj(sShadowMap, vec4(shadowPos.x, shadowPos.y + offsets.y, shadowPos.zw)).r * shadowPos.w > shadowPos.z,
            texture2DProj(sShadowMap, vec4(shadowPos.xy + offsets.xy, shadowPos.zw)).r * shadowPos.w > shadowPos.z
        );
        return cShadowIntensity.y + dot(inLight, vec4(cShadowIntensity.x));
    #elif defined(VSM_SHADOW)
        vec2 samples = texture2D(sShadowMap, shadowPos.xy / shadowPos.w).rg;
        return cShadowIntensity.y + cShadowIntensity.x * Chebyshev(samples, shadowPos.z / shadowPos.w);
    #endif
}
#endif

#ifdef POINTLIGHT
vec3 GetPointShadowTexCoord(vec3 vec)
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
    uv *= cShadowCubeUVBias.xy;
    return vec3(uv * 0.5 * invDominantAxis + vec2(0.5, 0.5) + faceOffset, invDominantAxis);
}

float GetPointShadow(vec3 lightVec)
{
    vec3 uvDepth = GetPointShadowTexCoord(lightVec);
    uvDepth.xy = uvDepth.xy * cShadowCubeAdjust.xy + cShadowCubeAdjust.zw;
    uvDepth.z = cShadowDepthFade.x + cShadowDepthFade.y * uvDepth.z;
    return GetShadow(vec4(uvDepth, 1.0));
}
#endif

#ifdef DIRLIGHT
float GetDirShadowFade(float inLight, float depth)
{
    return min(inLight + max((depth - cShadowDepthFade.z) * cShadowDepthFade.w, 0.0), 1.0);
}

#if !defined(GL_ES) || defined(WEBGL)
float GetDirShadow(const vec4 iShadowPos[NUMCASCADES], float depth)
{
    vec4 shadowPos;

    if (depth < cShadowSplits.x)
        shadowPos = iShadowPos[0];
    else if (depth < cShadowSplits.y)
        shadowPos = iShadowPos[1];
    else if (depth < cShadowSplits.z)
        shadowPos = iShadowPos[2];
    else
        shadowPos = iShadowPos[3];

    return GetDirShadowFade(GetShadow(shadowPos), depth);
}
#else
float GetDirShadow(const highp vec4 iShadowPos[NUMCASCADES], float depth)
{
    return GetDirShadowFade(GetShadow(iShadowPos[0]), depth);
}
#endif

#ifndef GL_ES
float GetDirShadowDeferred(vec4 projWorldPos, vec3 normal, float depth)
{
    vec4 shadowPos;

    /*#ifdef NORMALOFFSET
        float cosAngle = clamp(1.0 - dot(normal, cLightDir), 0.0, 1.0);
        if (depth < cShadowSplits.x)
            shadowPos = vec4(projWorldPos.xyz + cosAngle * cNormalOffsetScalePS.x * normal, 1.0) * cLightMatrices[0];
        else if (depth < cShadowSplits.y)
            shadowPos = vec4(projWorldPos.xyz + cosAngle * cNormalOffsetScalePS.y * normal, 1.0) * cLightMatrices[1];
        else if (depth < cShadowSplits.z)
            shadowPos = vec4(projWorldPos.xyz + cosAngle * cNormalOffsetScalePS.z * normal, 1.0) * cLightMatrices[2];
        else
            shadowPos = vec4(projWorldPos.xyz + cosAngle * cNormalOffsetScalePS.w * normal, 1.0) * cLightMatrices[3];
    #else*/
        if (depth < cShadowSplits.x)
            shadowPos = projWorldPos * cLightMatrices[0];
        else if (depth < cShadowSplits.y)
            shadowPos = projWorldPos * cLightMatrices[1];
        else if (depth < cShadowSplits.z)
            shadowPos = projWorldPos * cLightMatrices[2];
        else
            shadowPos = projWorldPos * cLightMatrices[3];
    //#endif

    return GetDirShadowFade(GetShadow(shadowPos), depth);
}
#endif
#endif

/*#ifndef GL_ES
float GetShadow(const vec4 iShadowPos[NUMCASCADES], float depth)
#else
float GetShadow(const highp vec4 iShadowPos[NUMCASCADES], float depth)
#endif
{
    #if defined(DIRLIGHT)
        return GetDirShadow(iShadowPos, depth);
    #elif defined(SPOTLIGHT)
        return GetShadow(iShadowPos[0]);
    #else
        return GetPointShadow(iShadowPos[0].xyz);
    #endif
}

#ifndef GL_ES
float GetShadowDeferred(vec4 projWorldPos, vec3 normal, float depth)
{
    #ifdef DIRLIGHT
        return GetDirShadowDeferred(projWorldPos, normal, depth);
    #else
        #ifdef NORMALOFFSET
            float cosAngle = clamp(1.0 - dot(normal, normalize(cLightPos.xyz - projWorldPos.xyz)), 0.0, 1.0);
            projWorldPos.xyz += cosAngle * cNormalOffsetScalePS.x * normal;
        #endif

        #ifdef SPOTLIGHT
            vec4 shadowPos = projWorldPos * cLightMatrices[1];
            return GetShadow(shadowPos);
        #else
            vec3 shadowPos = projWorldPos.xyz - cLightPos.xyz;
            return GetPointShadow(shadowPos);
        #endif
    #endif
}
#endif*/
#endif
#endif
