/// _Uniforms.glsl
/// Uniforms provied by the engine or standard material uniforms.
#ifndef _UNIFORMS_GLSL_
#define _UNIFORMS_GLSL_

#ifndef _CONFIG_GLSL_
    #error Include _Config.glsl before _Uniforms.glsl
#endif

UNIFORM_BUFFER_BEGIN(0, Frame)
    /// Time elapsed since previous frame.
    UNIFORM_HIGHP(float cDeltaTime)
    /// Time elapsed since scene started updating. Avoid using it.
    UNIFORM_HIGHP(float cElapsedTime)
UNIFORM_BUFFER_END(0, Frame)

UNIFORM_BUFFER_BEGIN(1, Camera)
    /// World to view space matrix.
    UNIFORM_HIGHP(mat4 cView)
    /// Inversed world to view space matrix.
    UNIFORM_HIGHP(mat4 cViewInv)
    /// World to clip space matrix.
    UNIFORM_HIGHP(mat4 cViewProj)
    /// Clip plane.
    UNIFORM_HIGHP(vec4 cClipPlane)
    /// Camera position in world space.
    UNIFORM_HIGHP(vec3 cCameraPos)
    /// Near clip distance of camera.
    UNIFORM_HIGHP(float cNearClip)
    /// Far clip distance of camera.
    UNIFORM_HIGHP(float cFarClip)
    UNIFORM_HIGHP(vec4 cDepthMode)
    UNIFORM_HIGHP(vec3 cFrustumSize)
    UNIFORM_HIGHP(vec4 cGBufferOffsets)
    UNIFORM_HIGHP(vec4 cDepthReconstruct)
    UNIFORM_HIGHP(vec2 cGBufferInvSize)
    /// Constant ambient color in current color space.
    UNIFORM(half4 cAmbientColor)
    UNIFORM(half4 cFogParams)
    /// Color of fog.
    UNIFORM(half3 cFogColor)
    /// Scale of normal shadow bias.
    UNIFORM(half cNormalOffsetScale)
UNIFORM_BUFFER_END(1, Camera)

/// Zone: Reflection probe parameters.
/// Disabled if pass has no ambient lighting.
#ifdef URHO3D_AMBIENT_PASS
UNIFORM_BUFFER_BEGIN(2, Zone)
    /// Average reflection color in linear space used to approximate roughness if textureCubeLod is not supported.
    UNIFORM(half3 cReflectionAverageColor)
    /// Multiplier used to convert roughness factor to LOD of reflection cubemap.
    UNIFORM(half cRoughnessToLODFactor)
UNIFORM_BUFFER_END(2, Zone)
#endif

UNIFORM_BUFFER_BEGIN(3, Light)
    /// Light position in world space.
    UNIFORM_HIGHP(vec4 cLightPos)
    /// Light direction in world space.
    UNIFORM(half3 cLightDir)
    /// x: cos(spotAngle / 2);
    /// y: 1 / (1 - x).
    UNIFORM(half2 cSpotAngle)
#ifdef URHO3D_LIGHT_CUSTOM_SHAPE
    /// Matrix from world to light texture space.
    UNIFORM_HIGHP(mat4 cLightShapeMatrix)
#endif
#ifdef URHO3D_NUM_VERTEX_LIGHTS
    /// Vertex lights data:
    /// [0].rgb: Light color in current color space;
    /// [0].a: Inverse range;
    /// [1].xyz: Light direction in world space;
    /// [1].w: Same as cSpotAngle.x;
    /// [2].xyz: Light position in world space;
    UNIFORM_HIGHP(vec4 cVertexLights[URHO3D_NUM_VERTEX_LIGHTS * 3])
#endif
    /// For directional and spot lights: Matrices from world to shadow texture space.
    /// For point lights: unused.
    UNIFORM_HIGHP(mat4 cLightMatrices[4])
    /// Light color in current color space.
    UNIFORM(half4 cLightColor)
    UNIFORM(half4 cShadowCubeAdjust)
    UNIFORM(half2 cShadowCubeUVBias)
    UNIFORM(half4 cShadowDepthFade)
    UNIFORM(half2 cShadowIntensity)
    UNIFORM(half2 cShadowMapInvSize)
    UNIFORM(half4 cShadowSplits)
#ifdef URHO3D_VARIANCE_SHADOW_MAP
    UNIFORM(half2 cVSMShadowParams)
#endif
#ifdef URHO3D_PHYSICAL_MATERIAL
    UNIFORM(half cLightRad)
    UNIFORM(half cLightLength)
#endif
UNIFORM_BUFFER_END(3, Light)

#define DEFAULT_MATERIAL_UNIFORMS \
    UNIFORM(half4 cUOffset) \
    UNIFORM(half4 cVOffset) \
    UNIFORM(half4 cLMOffset) \
    UNIFORM(half4 cMatDiffColor) \
    UNIFORM(half3 cMatEmissiveColor) \
    UNIFORM(half cRoughness) \
    UNIFORM(half3 cMatEnvMapColor) \
    UNIFORM(half cMetallic) \
    UNIFORM(half4 cMatSpecColor) \
    UNIFORM(half2 cFadeOffsetScale) \
    UNIFORM(half cNormalScale) \
    UNIFORM(half cDielectricReflectance)

/// cLMOffset.xy: Scale applied to lightmap UVs;
/// cLMOffset.zw: Offset applied to lightmap UVs.
#ifndef URHO3D_CUSTOM_MATERIAL_UNIFORMS
UNIFORM_BUFFER_BEGIN(4, Material)
    DEFAULT_MATERIAL_UNIFORMS
UNIFORM_BUFFER_END(4, Material)
#endif

/// Object: Per-instance constants
#ifdef URHO3D_VERTEX_SHADER
    #ifdef URHO3D_INSTANCING
        VERTEX_INPUT(vec4 iTexCoord4)
        VERTEX_INPUT(vec4 iTexCoord5)
        VERTEX_INPUT(vec4 iTexCoord6)
        #define cModel mat4(iTexCoord4, iTexCoord5, iTexCoord6, vec4(0.0, 0.0, 0.0, 1.0))

    #if defined(URHO3D_AMBIENT_DIRECTIONAL)
        VERTEX_INPUT(half4 iTexCoord7)
        VERTEX_INPUT(half4 iTexCoord8)
        VERTEX_INPUT(half4 iTexCoord9)
        VERTEX_INPUT(half4 iTexCoord10)
        VERTEX_INPUT(half4 iTexCoord11)
        VERTEX_INPUT(half4 iTexCoord12)
        VERTEX_INPUT(half4 iTexCoord13)
        #define cSHAr iTexCoord7
        #define cSHAg iTexCoord8
        #define cSHAb iTexCoord9
        #define cSHBr iTexCoord10
        #define cSHBg iTexCoord11
        #define cSHBb iTexCoord12
        #define cSHC  iTexCoord13
    #elif defined(URHO3D_AMBIENT_FLAT)
        VERTEX_INPUT(half4 iTexCoord7)
        #define cAmbient iTexCoord7
    #endif
    #else
        UNIFORM_BUFFER_BEGIN(5, Object)
            /// Object to world space matrix.
            UNIFORM_HIGHP(mat4 cModel)

            /// Per-object ambient lighting.
            /// If flat, in current color space.
            /// If directional, in linear space.
        #if defined(URHO3D_AMBIENT_DIRECTIONAL)
            UNIFORM(half4 cSHAr)
            UNIFORM(half4 cSHAg)
            UNIFORM(half4 cSHAb)
            UNIFORM(half4 cSHBr)
            UNIFORM(half4 cSHBg)
            UNIFORM(half4 cSHBb)
            UNIFORM(half4 cSHC)
        #elif defined(URHO3D_AMBIENT_FLAT)
            UNIFORM(half4 cAmbient)
        #endif
        #ifdef URHO3D_GEOMETRY_BILLBOARD
            /// Rotation of billboard plane in world space.
            UNIFORM(mediump mat3 cBillboardRot)
        #endif
        #ifdef URHO3D_GEOMETRY_SKINNED
            /// Object to world space matrices for each bone.
            UNIFORM_HIGHP(vec4 cSkinMatrices[MAXBONES * 3])
        #endif
        UNIFORM_BUFFER_END(5, Object)
    #endif
#endif // URHO3D_VERTEX_SHADER

#endif // _UNIFORMS_GLSL_
