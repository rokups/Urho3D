#ifdef COMPILEPS
SAMPLER(0, sampler2D sDiffMap)
SAMPLER(0, samplerCube sDiffCubeMap)
SAMPLER(1, sampler2D sNormalMap)
SAMPLER(2, sampler2D sSpecMap)
SAMPLER(3, sampler2D sEmissiveMap)
SAMPLER(4, sampler2D sEnvMap)
SAMPLER(4, samplerCube sEnvCubeMap)
SAMPLER(8, sampler2D sLightRampMap)
SAMPLER(9, sampler2D sLightSpotMap)
SAMPLER(9, samplerCube sLightCubeMap)
#ifndef GL_ES
    SAMPLER(5, sampler3D sVolumeMap)
    SAMPLER(0, sampler2D sAlbedoBuffer)
    SAMPLER(1, sampler2D sNormalBuffer)
    SAMPLER(13, sampler2D sDepthBuffer)
    SAMPLER(14, sampler2D sLightBuffer)
    #ifdef VSM_SHADOW
        SAMPLER(10, sampler2D sShadowMap)
    #else
        SAMPLER(10, sampler2DShadow sShadowMap)
    #endif
    SAMPLER(15, samplerCube sZoneCubeMap)
    SAMPLER(15, sampler3D sZoneVolumeMap)
#else
    SAMPLER(10, highp sampler2D sShadowMap)
#endif

#ifdef GL3
#define texture2D texture
#define texture2DProj textureProj
#define texture3D texture
#define textureCube texture
#define texture2DLod textureLod
#define texture2DLodOffset textureLodOffset
#endif

#ifdef URHO3D_MATERIAL_HAS_DIFFUSE
    #define Color_DiffMapToLinear(color) MATERIAL_DIFFUSE_TEXTURE_LINEAR(color)
    #define Color_DiffMapToGamma(color)  MATERIAL_DIFFUSE_TEXTURE_GAMMA(color)
    #ifdef URHO3D_GAMMA_CORRECTION
        #define Color_DiffMapToLight(color) Color_DiffMapToLinear(color)
    #else
        #define Color_DiffMapToLight(color) Color_DiffMapToGamma(color)
    #endif
#endif

vec3 DecodeNormal(vec4 normalInput)
{
    #ifdef PACKEDNORMAL
        vec3 normal;
        normal.xy = normalInput.ag * 2.0 - 1.0;
        normal.z = sqrt(max(1.0 - dot(normal.xy, normal.xy), 0.0));
        return normal;
    #else
        return normalize(normalInput.rgb * 2.0 - 1.0);
    #endif
}

vec3 EncodeDepth(float depth)
{
    #ifndef GL3
        vec3 ret;
        depth *= 255.0;
        ret.x = floor(depth);
        depth = (depth - ret.x) * 255.0;
        ret.y = floor(depth);
        ret.z = (depth - ret.y);
        ret.xy *= 1.0 / 255.0;
        return ret;
    #else
        // OpenGL 3 can use different MRT formats, so no need for encoding
        return vec3(depth, 0.0, 0.0);
    #endif
}

float DecodeDepth(vec3 depth)
{
    #ifndef GL3
        const vec3 dotValues = vec3(1.0, 1.0 / 255.0, 1.0 / (255.0 * 255.0));
        return dot(depth, dotValues);
    #else
        // OpenGL 3 can use different MRT formats, so no need for encoding
        return depth.r;
    #endif
}

float ReconstructDepth(float hwDepth)
{
    return dot(vec2(hwDepth, cDepthReconstruct.y / (hwDepth - cDepthReconstruct.x)), cDepthReconstruct.zw);
}
#endif
