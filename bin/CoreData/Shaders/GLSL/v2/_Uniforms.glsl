/// _Uniforms.glsl
/// Uniforms provied by the engine or standard material uniforms.
#ifndef _UNIFORMS_GLSL_
#define _UNIFORMS_GLSL_

#ifndef _CONFIG_GLSL_
    #error Include _Config.glsl before _Uniforms.glsl
#endif

UNIFORM_BUFFER_BEGIN(0, Frame)
    /// Time elapsed since previous frame, in seconds.
    UNIFORM_HIGHP(float cDeltaTime)
    /// Time elapsed since scene started updating, in seconds. Avoid using it.
    UNIFORM_HIGHP(float cElapsedTime)
UNIFORM_BUFFER_END(0, Frame)

UNIFORM_BUFFER_BEGIN(1, Camera)
    /// World to view space matrix.
    UNIFORM_HIGHP(mat4 cView)
    /// Inversed world to view space matrix.
    UNIFORM_HIGHP(mat4 cViewInv)
    /// World to clip space matrix.
    UNIFORM_HIGHP(mat4 cViewProj)
    /// Clip plane in clip space.
    UNIFORM_HIGHP(vec4 cClipPlane)
    /// Camera position in world space.
    UNIFORM_HIGHP(vec3 cCameraPos)
    /// Distance to near clip plane in units.
    UNIFORM_HIGHP(float cNearClip)
    /// Distance to far clip plane in units.
    UNIFORM_HIGHP(float cFarClip)
    /// x: 1 for orthographic projection, 0 for perspective projection.
    /// y: Unused.
    /// zw: Factors used to convert clip position zw to linear depth. depth=1 is far plane.
    ///     For orthographic projection depth=0 is near plane.
    ///     For perspective projection depth=0 is focus point.
    /// TODO(legacy): Don't need xy, can use cDepthReconstruct instead.
    UNIFORM_HIGHP(vec4 cDepthMode)
    /// xy: Linear dimensions of far clip plane in units.
    /// z: Distance to far clip plane.
    /// TODO(legacy): Probably don't need z component, should be identical to cFarClip.
    UNIFORM_HIGHP(vec3 cFrustumSize)
    /// Transform that is applied to clip position xy to get underlying texture uv.
    /// Basically describes current viewport.
    /// xy: Translation part.
    /// zw: Scaling part.
    UNIFORM_HIGHP(vec4 cGBufferOffsets)
    /// xy: Factors used to convert value from depth buffer to linear depth for perspective projection.
    /// z: 1 for orthographic projection, 0 for perspective projection.
    /// w: 0 for orthographic projection, 1 for perspective projection.
    UNIFORM_HIGHP(vec4 cDepthReconstruct)
    /// (1, 1) divided by viewport size.
    UNIFORM_HIGHP(vec2 cGBufferInvSize)
    /// xyz: Constant ambient color in current color space.
    /// w: Unused.
    UNIFORM(half4 cAmbientColor)
    /// x: Linear depth corresponding to fog end.
    /// y: Inverted difference between linear depth where fog ends and where fog begins.
    /// zw: Unused.
    UNIFORM(half4 cFogParams)
    /// Color of the fog in current color space.
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
#ifndef URHO3D_DEPTH_ONLY_PASS
    /// Spot light cutoff parameters, see Light::GetCutoffParams() for details.
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
    UNIFORM_HIGHP(mat4 cLightMatrices[URHO3D_MAX_SHADOW_CASCADES])
    /// Light color in current color space.
    UNIFORM(half4 cLightColor)
    /// Transforms cubemap UV in range [0, 3]x[0, 2] to actual shadow map UVs.
    /// xy: Scaling.
    /// zw: Translation.
    UNIFORM(half4 cShadowCubeAdjust)
    /// Size of actual used shadow map (without padding) relative to size of split region.
    UNIFORM(half2 cShadowCubeUVBias)
    /// xy: Used to convert inverted linear depth to shadow map depth in range [0, 1] for point and spot lights.
    /// z: Linear depth where shadow fade out begins, for directional lights.
    /// w: Inverted difference between linear depth where shadow fade out ends and where shadow fade out begins.
    UNIFORM(half4 cShadowDepthFade)
    /// x: One minus light intensity in shadow. Also multiplied by 0.25 for 2x2 PCF shadows.
    /// y: Light intensity in shadow
    UNIFORM(half2 cShadowIntensity)
    /// (1, 1) divided by shadow map size, used to emulate pixel offsets when necessary.
    UNIFORM(half2 cShadowMapInvSize)
    /// x: Depth where 1st split ends.
    /// y: Depth where 2nd split ends.
    /// z: Depth where 3rd split ends.
    /// w: Depth where 4th split ends.
    UNIFORM(half4 cShadowSplits)
#ifdef URHO3D_VARIANCE_SHADOW_MAP
    /// Variance shadow map parameters.
    UNIFORM_HIGHP(vec2 cVSMShadowParams)
#endif
#ifdef URHO3D_PHYSICAL_MATERIAL
    /// Radius of sphere and tube lights. Unused.
    UNIFORM(half cLightRad)
    /// Length of tube lights. Unused.
    UNIFORM(half cLightLength)
#endif
#endif
UNIFORM_BUFFER_END(3, Light)

/// Uniforms needed for UV transformation.
/// cUOffset: U coordinate transformation: u <- dot(uv, xy) + w.
/// cVOffset: V coordinate transformation: v <- dot(uv, xy) + w.
#define UNIFORMS_UV_TRANSFORM \
    UNIFORM(half4 cUOffset) \
    UNIFORM(half4 cVOffset)

/// Uniforms needed for lightmapped material.
/// cLMOffset: Transforms model lightmap UVs to UVs in lightmap: uv <- uv*xy + zw.
#ifdef URHO3D_HAS_LIGHTMAP
    #define UNIFORMS_LIGHTMAP \
        UNIFORM(half4 cLMOffset)
#else
    #define UNIFORMS_LIGHTMAP
#endif

/// Unifroms needed for material surface evaluation.
/// cMatDiffColor.rgb: Material diffuse color in gamma space, or base color for PBR rendering.
/// cMatDiffColor.a: Material transparency.
/// cMatEmissiveColor: Material emissive color in gamma space.
/// cRoughness: Perceptual roughness of PBR material.
/// cMatEnvMapColor: Environment reflection color for non-PBR material.
/// cMetallic: Metallness of PBR material.
/// cMatSpecColor.rgb: Color of specular reflection for non-PBR material.
/// cMatSpecColor.a: Specular reflection power for non-PBR material.
/// cFadeOffsetScale.x: Difference in linear depth between background surface and geometry
///                     where geometry with soft fade out is completely transparent.
/// cFadeOffsetScale.y: Inverted difference between linear depths
///                     where geometry is fully visible and is fully invisible.
/// cNormalScale: Scale applied to normals in normal map.
/// cDielectricReflectance: Normalized reflectance of dielectric surfaces.
///                         Physical range is [0, 1] but it's okay to push beyond.
#define UNIFORMS_SURFACE \
    UNIFORM(half4 cMatDiffColor) \
    UNIFORM(half3 cMatEmissiveColor) \
    UNIFORM(half cRoughness) \
    UNIFORM(half3 cMatEnvMapColor) \
    UNIFORM(half cMetallic) \
    UNIFORM(half4 cMatSpecColor) \
    UNIFORM(half2 cFadeOffsetScale) \
    UNIFORM(half cNormalScale) \
    UNIFORM(half cDielectricReflectance)

/// Uniforms needed for planar reflections.
/// cReflectionPlaneX: Dot with world normal to get distortion in screen texture X component.
/// cReflectionPlaneY: Dot with world normal to get distortion in screen texture Y component.
#ifdef URHO3D_MATERIAL_HAS_PLANAR_ENVIRONMENT
    #define UNIFORMS_PLANAR_REFLECTION \
        UNIFORM(half4 cReflectionPlaneX) \
        UNIFORM(half4 cReflectionPlaneY)
#else
    #define UNIFORMS_PLANAR_REFLECTION
#endif

#define DEFAULT_MATERIAL_UNIFORMS \
    UNIFORMS_UV_TRANSFORM \
    UNIFORMS_LIGHTMAP \
    UNIFORMS_SURFACE \
    UNIFORMS_PLANAR_REFLECTION

/// cLMOffset.xy: Scale applied to lightmap UVs;
/// cLMOffset.zw: Offset applied to lightmap UVs.
#ifndef URHO3D_CUSTOM_MATERIAL_UNIFORMS
UNIFORM_BUFFER_BEGIN(4, Material)
    DEFAULT_MATERIAL_UNIFORMS
UNIFORM_BUFFER_END(4, Material)
#endif

/// Object: Per-instance constants.
/// cModel: Object to world space matrix.
/// cAmbient: Per-object ambient lighting in current color space.
/// cSH*: Per-object ambient lighting in linear color space.
/// cBillboardRot: Rotation of billboard plane in world space.
/// cSkinMatrices: Object to world space matrices for each bone.
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
            UNIFORM_HIGHP(mat4 cModel)

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
