#ifndef _UNIFORMS_GLSL_
#define _UNIFORMS_GLSL_

CBUFFER_BEGIN(0, Frame)
    CBUFFER_UNIFORM(float cDeltaTime)
    CBUFFER_UNIFORM(float cElapsedTime)
CBUFFER_END()

CBUFFER_BEGIN(1, Camera)
    CBUFFER_UNIFORM(vec3 cCameraPos)
    CBUFFER_UNIFORM(float cNearClip)
    CBUFFER_UNIFORM(float cFarClip)
    CBUFFER_UNIFORM(vec4 cDepthMode)
    CBUFFER_UNIFORM(vec3 cFrustumSize)
    CBUFFER_UNIFORM(vec4 cGBufferOffsets)
    CBUFFER_UNIFORM(mat4 cView)
    CBUFFER_UNIFORM(mat4 cViewInv)
    CBUFFER_UNIFORM(mat4 cViewProj)
    CBUFFER_UNIFORM(vec4 cClipPlane)
    CBUFFER_UNIFORM(vec4 cDepthReconstruct)
    CBUFFER_UNIFORM(vec2 cGBufferInvSize)
CBUFFER_END()

CBUFFER_BEGIN(2, Zone)
    CBUFFER_UNIFORM(vec4 cAmbientColor)
    CBUFFER_UNIFORM(vec4 cFogParams)
    CBUFFER_UNIFORM(vec3 cFogColor)
CBUFFER_END()

CBUFFER_BEGIN(3, Light)
    CBUFFER_UNIFORM(vec4 cLightPos)
    CBUFFER_UNIFORM(vec3 cLightDir)
    CBUFFER_UNIFORM(vec4 cNormalOffsetScale)
#ifdef NUMVERTEXLIGHTS
    CBUFFER_UNIFORM(vec4 cVertexLights[4 * 3])
#endif
    CBUFFER_UNIFORM(mat4 cLightMatrices[4])

    CBUFFER_UNIFORM(vec4 cLightColor)
    CBUFFER_UNIFORM(vec4 cShadowCubeAdjust)
    CBUFFER_UNIFORM(vec2 cShadowCubeUVBias)
    CBUFFER_UNIFORM(vec4 cShadowDepthFade)
    CBUFFER_UNIFORM(vec2 cShadowIntensity)
    CBUFFER_UNIFORM(vec2 cShadowMapInvSize)
    CBUFFER_UNIFORM(vec4 cShadowSplits)
#ifdef VSM_SHADOW
    CBUFFER_UNIFORM(vec2 cVSMShadowParams)
#endif
#ifdef PBR
    CBUFFER_UNIFORM(float cLightRad)
    CBUFFER_UNIFORM(float cLightLength)
#endif
CBUFFER_END()

#ifndef CUSTOM_MATERIAL_CBUFFER
CBUFFER_BEGIN(4, Material)
    CBUFFER_UNIFORM(vec4 cUOffset)
    CBUFFER_UNIFORM(vec4 cVOffset)
    CBUFFER_UNIFORM(vec4 cLMOffset)

    CBUFFER_UNIFORM(vec4 cMatDiffColor)
    CBUFFER_UNIFORM(vec3 cMatEmissiveColor)
    CBUFFER_UNIFORM(vec3 cMatEnvMapColor)
    CBUFFER_UNIFORM(vec4 cMatSpecColor)
#ifdef PBR
    CBUFFER_UNIFORM(float cRoughness)
    CBUFFER_UNIFORM(float cMetallic)
#endif
CBUFFER_END()
#endif

#ifdef COMPILEVS
CBUFFER_BEGIN(5, Object)
    CBUFFER_UNIFORM(mat4 cModel)
#ifdef SPHERICALHARMONICS
    CBUFFER_UNIFORM(vec4 cSHAr)
    CBUFFER_UNIFORM(vec4 cSHAg)
    CBUFFER_UNIFORM(vec4 cSHAb)
    CBUFFER_UNIFORM(vec4 cSHBr)
    CBUFFER_UNIFORM(vec4 cSHBg)
    CBUFFER_UNIFORM(vec4 cSHBb)
    CBUFFER_UNIFORM(vec4 cSHC)
#else
    CBUFFER_UNIFORM(vec4 cAmbient)
#endif
#ifdef GEOM_BILLBOARD
    CBUFFER_UNIFORM(mat3 cBillboardRot)
#endif
#ifdef GEOM_SKINNED
    CBUFFER_UNIFORM(vec4 cSkinMatrices[MAXBONES * 3])
#endif
CBUFFER_END()
#endif

#endif
