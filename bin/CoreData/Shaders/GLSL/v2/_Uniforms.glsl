#ifndef _UNIFORMS_GLSL_
#define _UNIFORMS_GLSL_

CBUFFER_BEGIN(0, Frame)
    CBUFFER_UNIFORM(mediump float cDeltaTime)
    CBUFFER_UNIFORM(mediump float cElapsedTime)
CBUFFER_END()

CBUFFER_BEGIN(1, Camera)
    CBUFFER_UNIFORM(mediump vec3 cCameraPos) // TODO(renderer): Increase precision
    CBUFFER_UNIFORM(mediump float cNearClip)
    CBUFFER_UNIFORM(mediump float cFarClip)
    CBUFFER_UNIFORM(mediump vec4 cDepthMode)
    CBUFFER_UNIFORM(mediump vec3 cFrustumSize)
    CBUFFER_UNIFORM(mediump vec4 cGBufferOffsets)
    CBUFFER_UNIFORM(highp mat4 cView)
    CBUFFER_UNIFORM(highp mat4 cViewInv)
    CBUFFER_UNIFORM(highp mat4 cViewProj)
    CBUFFER_UNIFORM(highp vec4 cClipPlane)
    CBUFFER_UNIFORM(mediump vec4 cDepthReconstruct)
    CBUFFER_UNIFORM(mediump vec2 cGBufferInvSize)
    CBUFFER_UNIFORM(mediump vec4 cAmbientColor)
    CBUFFER_UNIFORM(mediump vec4 cFogParams)
    CBUFFER_UNIFORM(mediump vec3 cFogColor)
CBUFFER_END()

CBUFFER_BEGIN(3, Light)
    CBUFFER_UNIFORM(mediump vec4 cLightPos) // TODO(renderer): Increase precision
    CBUFFER_UNIFORM(mediump vec3 cLightDir)
    CBUFFER_UNIFORM(mediump vec4 cNormalOffsetScale)
#ifdef NUMVERTEXLIGHTS
    CBUFFER_UNIFORM_VS(highp vec4 cVertexLights[4 * 3])
#endif
    CBUFFER_UNIFORM(mediump mat4 cLightMatrices[4])

    CBUFFER_UNIFORM(mediump vec4 cLightColor)
    CBUFFER_UNIFORM(mediump vec4 cShadowCubeAdjust)
    CBUFFER_UNIFORM(mediump vec2 cShadowCubeUVBias)
    CBUFFER_UNIFORM(mediump vec4 cShadowDepthFade)
    CBUFFER_UNIFORM(mediump vec2 cShadowIntensity)
    CBUFFER_UNIFORM(mediump vec2 cShadowMapInvSize)
    CBUFFER_UNIFORM(mediump vec4 cShadowSplits)
#ifdef VSM_SHADOW
    CBUFFER_UNIFORM(mediump vec2 cVSMShadowParams)
#endif
#ifdef PBR
    CBUFFER_UNIFORM(mediump float cLightRad)
    CBUFFER_UNIFORM(mediump float cLightLength)
#endif
CBUFFER_END()

#ifndef CUSTOM_MATERIAL_CBUFFER
CBUFFER_BEGIN(4, Material)
    CBUFFER_UNIFORM(mediump vec4 cUOffset)
    CBUFFER_UNIFORM(mediump vec4 cVOffset)
    CBUFFER_UNIFORM(mediump vec4 cLMOffset)

    CBUFFER_UNIFORM(mediump vec4 cMatDiffColor)
    CBUFFER_UNIFORM(mediump vec3 cMatEmissiveColor)
    CBUFFER_UNIFORM(mediump vec3 cMatEnvMapColor)
    CBUFFER_UNIFORM(mediump vec4 cMatSpecColor)
#ifdef PBR
    CBUFFER_UNIFORM(mediump float cRoughness)
    CBUFFER_UNIFORM(mediump float cMetallic)
#endif
CBUFFER_END()
#endif

#ifdef STAGE_VERTEX_SHADER
CBUFFER_BEGIN(5, Object)
    CBUFFER_UNIFORM(highp mat4 cModel)

#if defined(URHO3D_AMBIENT_DIRECTIONAL)
    CBUFFER_UNIFORM(mediump vec4 cSHAr)
    CBUFFER_UNIFORM(mediump vec4 cSHAg)
    CBUFFER_UNIFORM(mediump vec4 cSHAb)
    CBUFFER_UNIFORM(mediump vec4 cSHBr)
    CBUFFER_UNIFORM(mediump vec4 cSHBg)
    CBUFFER_UNIFORM(mediump vec4 cSHBb)
    CBUFFER_UNIFORM(mediump vec4 cSHC)
#elif defined(URHO3D_AMBIENT_FLAT)
    CBUFFER_UNIFORM(mediump vec4 cAmbient)
#endif

#ifdef GEOM_BILLBOARD
    CBUFFER_UNIFORM(mediump mat3 cBillboardRot)
#endif
#ifdef GEOM_SKINNED
    CBUFFER_UNIFORM(highp vec4 cSkinMatrices[MAXBONES * 3])
#endif
CBUFFER_END()
#endif

#endif
