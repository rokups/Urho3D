#if !defined(GL3) || !defined(USE_CBUFFERS)
    #define CBUFFER_BEGIN(name)
    #define CBUFFER_UNIFORM(decl) uniform decl;
    #define CBUFFER_END()
#else
    #define CBUFFER_BEGIN(name) uniform name {
    #define CBUFFER_UNIFORM(decl) decl;
    #define CBUFFER_END() };
#endif

CBUFFER_BEGIN(Frame)
    CBUFFER_UNIFORM(float cDeltaTime)
    CBUFFER_UNIFORM(float cElapsedTime)

    CBUFFER_UNIFORM(float cDeltaTimePS)
    CBUFFER_UNIFORM(float cElapsedTimePS)
CBUFFER_END()

CBUFFER_BEGIN(Camera)
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

    CBUFFER_UNIFORM(vec3 cCameraPosPS)
    CBUFFER_UNIFORM(vec4 cDepthReconstruct)
    CBUFFER_UNIFORM(vec2 cGBufferInvSize)
    CBUFFER_UNIFORM(float cNearClipPS)
    CBUFFER_UNIFORM(float cFarClipPS)
CBUFFER_END()

CBUFFER_BEGIN(Zone)
    CBUFFER_UNIFORM(vec3 cAmbientStartColor)
    CBUFFER_UNIFORM(vec3 cAmbientEndColor)
    CBUFFER_UNIFORM(mat4 cZone)

    CBUFFER_UNIFORM(vec4 cAmbientColor)
    CBUFFER_UNIFORM(vec4 cFogParams)
    CBUFFER_UNIFORM(vec3 cFogColor)
    CBUFFER_UNIFORM(vec3 cZoneMin)
    CBUFFER_UNIFORM(vec3 cZoneMax)
CBUFFER_END()

CBUFFER_BEGIN(Light)
    CBUFFER_UNIFORM(vec4 cLightPos)
    CBUFFER_UNIFORM(vec3 cLightDir)
    CBUFFER_UNIFORM(vec4 cNormalOffsetScale)
#ifdef NUMVERTEXLIGHTS
    CBUFFER_UNIFORM(vec4 cVertexLights[4 * 3])
#endif
    CBUFFER_UNIFORM(mat4 cLightMatrices[4])

    CBUFFER_UNIFORM(vec4 cLightColor)
    CBUFFER_UNIFORM(vec4 cLightPosPS)
    CBUFFER_UNIFORM(vec3 cLightDirPS)
    CBUFFER_UNIFORM(vec4 cNormalOffsetScalePS)
    CBUFFER_UNIFORM(vec4 cShadowCubeAdjust)
    CBUFFER_UNIFORM(vec4 cShadowDepthFade)
    CBUFFER_UNIFORM(vec2 cShadowIntensity)
    CBUFFER_UNIFORM(vec2 cShadowMapInvSize)
    CBUFFER_UNIFORM(vec4 cShadowSplits)
    CBUFFER_UNIFORM(mat4 cLightMatricesPS[4])
#ifdef VSM_SHADOW
    CBUFFER_UNIFORM(vec2 cVSMShadowParams)
#endif
#ifdef PBR
    CBUFFER_UNIFORM(float cLightRad)
    CBUFFER_UNIFORM(float cLightLength)
#endif
CBUFFER_END()

#ifndef CUSTOM_MATERIAL_CBUFFER
CBUFFER_BEGIN(Material)
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
CBUFFER_BEGIN(Object)
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
#ifdef BILLBOARD
    CBUFFER_UNIFORM(mat3 cBillboardRot)
#endif
#ifdef SKINNED
    CBUFFER_UNIFORM(vec4 cSkinMatrices[MAXBONES*3])
#endif
CBUFFER_END()
#endif
