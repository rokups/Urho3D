#ifndef _SAMPLERS_GLSL_
#define _SAMPLERS_GLSL_

#ifndef _CONFIG_GLSL_
    #error Include "_Config.glsl" before "_Samplers.glsl"
#endif

#ifdef URHO3D_PIXEL_SHADER

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
#ifdef URHO3D_VARIANCE_SHADOW_MAP
    SAMPLER(10, optional_highp sampler2D sShadowMap)
#else
    SAMPLER(10, optional_highp sampler2DShadow sShadowMap)
#endif
#ifndef GL_ES
    SAMPLER(5, sampler3D sVolumeMap)
    SAMPLER(13, sampler2D sDepthBuffer)
    SAMPLER(15, samplerCube sZoneCubeMap)
    SAMPLER(15, sampler3D sZoneVolumeMap)
#endif

#ifdef URHO3D_MATERIAL_HAS_DIFFUSE
    #define DiffMap_ToGamma(color)  CONCATENATE_2(Texture_ToGamma_,  URHO3D_MATERIAL_DIFFUSE_HINT)(color)
    #define DiffMap_ToLinear(color) CONCATENATE_2(Texture_ToLinear_, URHO3D_MATERIAL_DIFFUSE_HINT)(color)
    #define DiffMap_ToLight(color)  CONCATENATE_2(Texture_ToLight_,  URHO3D_MATERIAL_DIFFUSE_HINT)(color)
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

float ReconstructDepth(float hwDepth)
{
    return dot(vec2(hwDepth, cDepthReconstruct.y / (hwDepth - cDepthReconstruct.x)), cDepthReconstruct.zw);
}

#endif // URHO3D_PIXEL_SHADER
#endif // _SAMPLERS_GLSL_
