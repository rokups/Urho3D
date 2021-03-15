#ifndef _UNIFORMS_GLSL_
#define _UNIFORMS_GLSL_

UNIFORM_BUFFER_BEGIN(0, Frame)
    UNIFORM(mediump float cDeltaTime)
    UNIFORM(mediump float cElapsedTime)
UNIFORM_BUFFER_END()

UNIFORM_BUFFER_BEGIN(1, Camera)
    UNIFORM(mediump vec3 cCameraPos) // TODO(renderer): Increase precision
    UNIFORM(mediump float cNearClip)
    UNIFORM(mediump float cFarClip)
    UNIFORM_VERTEX(highp float cNormalOffsetScale)
    UNIFORM(mediump vec4 cDepthMode)
    UNIFORM(mediump vec3 cFrustumSize)
    UNIFORM(mediump vec4 cGBufferOffsets)
    UNIFORM_VERTEX(highp mat4 cView)
    UNIFORM_VERTEX(highp mat4 cViewInv)
    UNIFORM_VERTEX(highp mat4 cViewProj)
    UNIFORM_VERTEX(highp vec4 cClipPlane)
    UNIFORM(mediump vec4 cDepthReconstruct)
    UNIFORM(mediump vec2 cGBufferInvSize)
    UNIFORM(mediump vec4 cAmbientColor)
    UNIFORM(mediump vec4 cFogParams)
    UNIFORM(mediump vec3 cFogColor)
UNIFORM_BUFFER_END()

UNIFORM_BUFFER_BEGIN(3, Light)
    UNIFORM(mediump vec4 cLightPos) // TODO(renderer): Increase precision
    UNIFORM(mediump vec3 cLightDir)
    UNIFORM(mediump vec2 cSpotAngle)
#ifdef URHO3D_LIGHT_CUSTOM_SHAPE
    UNIFORM(mediump mat4 cLightShapeMatrix)
#endif
#ifdef URHO3D_NUM_VERTEX_LIGHTS
    UNIFORM_VERTEX(highp vec4 cVertexLights[URHO3D_NUM_VERTEX_LIGHTS * 3])
#endif
    UNIFORM(mediump mat4 cLightMatrices[4])

    UNIFORM(mediump vec4 cLightColor)
    UNIFORM(mediump vec4 cShadowCubeAdjust)
    UNIFORM(mediump vec2 cShadowCubeUVBias)
    UNIFORM(mediump vec4 cShadowDepthFade)
    UNIFORM(mediump vec2 cShadowIntensity)
    UNIFORM(mediump vec2 cShadowMapInvSize)
    UNIFORM(mediump vec4 cShadowSplits)
#ifdef VSM_SHADOW
    UNIFORM(mediump vec2 cVSMShadowParams)
#endif
#ifdef PBR
    UNIFORM(mediump float cLightRad)
    UNIFORM(mediump float cLightLength)
#endif
UNIFORM_BUFFER_END()

#ifndef CUSTOM_MATERIAL_CBUFFER
UNIFORM_BUFFER_BEGIN(4, Material)
    UNIFORM(mediump vec4 cUOffset)
    UNIFORM(mediump vec4 cVOffset)
    UNIFORM(mediump vec4 cLMOffset)

    UNIFORM(mediump vec4 cMatDiffColor)
    UNIFORM(mediump vec3 cMatEmissiveColor)
    UNIFORM(mediump vec3 cMatEnvMapColor)
    UNIFORM(mediump vec4 cMatSpecColor)
#ifdef PBR
    UNIFORM(mediump float cRoughness)
    UNIFORM(mediump float cMetallic)
#endif
UNIFORM_BUFFER_END()
#endif

// Object: Per-instance data
#ifdef URHO3D_VERTEX_SHADER
INSTANCE_BUFFER_BEGIN(5, Object)
    // Model-to-world transform matrix
    #ifdef URHO3D_INSTANCING
        VERTEX_INPUT(highp vec4 iTexCoord4)
        VERTEX_INPUT(highp vec4 iTexCoord5)
        VERTEX_INPUT(highp vec4 iTexCoord6)
        #define cModel mat4(iTexCoord4, iTexCoord5, iTexCoord6, vec4(0.0, 0.0, 0.0, 1.0))
    #else
        UNIFORM(highp mat4 cModel)
    #endif

    // Per-object ambient lighting
    #ifdef URHO3D_INSTANCING
        #if defined(URHO3D_AMBIENT_DIRECTIONAL)
            VERTEX_INPUT(mediump vec4 iTexCoord7)
            VERTEX_INPUT(mediump vec4 iTexCoord8)
            VERTEX_INPUT(mediump vec4 iTexCoord9)
            VERTEX_INPUT(mediump vec4 iTexCoord10)
            VERTEX_INPUT(mediump vec4 iTexCoord11)
            VERTEX_INPUT(mediump vec4 iTexCoord12)
            VERTEX_INPUT(mediump vec4 iTexCoord13)
            #define cSHAr iTexCoord7
            #define cSHAg iTexCoord8
            #define cSHAb iTexCoord9
            #define cSHBr iTexCoord10
            #define cSHBg iTexCoord11
            #define cSHBb iTexCoord12
            #define cSHC  iTexCoord13
        #elif defined(URHO3D_AMBIENT_FLAT)
            VERTEX_INPUT(mediump vec4 iTexCoord7)
            #define cAmbient iTexCoord7
        #endif
    #else
        #if defined(URHO3D_AMBIENT_DIRECTIONAL)
            UNIFORM(mediump vec4 cSHAr)
            UNIFORM(mediump vec4 cSHAg)
            UNIFORM(mediump vec4 cSHAb)
            UNIFORM(mediump vec4 cSHBr)
            UNIFORM(mediump vec4 cSHBg)
            UNIFORM(mediump vec4 cSHBb)
            UNIFORM(mediump vec4 cSHC)
        #elif defined(URHO3D_AMBIENT_FLAT)
            UNIFORM(mediump vec4 cAmbient)
        #endif
    #endif

    // Instancing is not supported for these geometry types:
    #ifdef URHO3D_GEOMETRY_BILLBOARD
        UNIFORM(mediump mat3 cBillboardRot)
    #endif
    #ifdef URHO3D_GEOMETRY_SKINNED
        UNIFORM(highp vec4 cSkinMatrices[MAXBONES * 3])
    #endif

INSTANCE_BUFFER_END()
#endif // URHO3D_VERTEX_SHADER

#endif // _UNIFORMS_GLSL_
