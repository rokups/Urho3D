/// _Config.glsl
/// Global configuration management, platform compatibility utilities,
/// global constants and helper functions.
/// Should be included before any other shader code.
///
/// Compatibility notes:
/// - Don't use function-style macros with 0 arguments, they don't work on some Android devices. Example (bad):
///   #define GetModelMatrix() cModel
/// - Don't change uniform buffer content depending on shader type or on shader-type-specifiec defines:
///   DX11 expects same buffers and will not handle it well.
///   Consider disabling whole buffer for stage.
///   Example: Object uniform buffer is enabled only for vertex shader.
#ifndef _CONFIG_GLSL_
#define _CONFIG_GLSL_

#extension GL_ARB_shading_language_420pack: enable
#extension GL_EXT_shader_texture_lod: enable
#extension GL_OES_standard_derivatives : enable

/// =================================== Constants ===================================

#define M_PI 3.14159265358979323846
#define M_EPSILON 0.0001

/// =================================== Types ===================================

/// Disable precision modifiers if not GL ES
#ifndef GL_ES
    #define highp
    #define mediump
    #define lowp
#endif

/// Define shortcuts for precision-qualified types
#define half   mediump float
#define half2  mediump vec2
#define half3  mediump vec3
#define half4  mediump vec4
#define fixed  lowp float
#define fixed2 lowp vec2
#define fixed3 lowp vec3
#define fixed4 lowp vec4

/// =================================== External defines ===================================

/// Whether to disable default uniform buffer in favor of custom one.
/// You may still need to declare default constants to use built-in includes.
// #define URHO3D_CUSTOM_MATERIAL_UNIFORMS

/// Configures what data vertex shader needs in VertexTransform:
// #define URHO3D_VERTEX_NEED_NORMAL
// #define URHO3D_VERTEX_NEED_TANGENT

/// Configures what data pixel shader needs:
// #define URHO3D_PIXEL_NEED_SCREEN_POSITION
// #define URHO3D_PIXEL_NEED_EYE_VECTOR
// #define URHO3D_PIXEL_NEED_NORMAL
// #define URHO3D_PIXEL_NEED_NORMAL_IN_TANGENT_SPACE
// #define URHO3D_PIXEL_NEED_TANGENT
// #define URHO3D_PIXEL_NEED_VERTEX_COLOR
// #define URHO3D_PIXEL_NEED_BACKGROUND_DEPTH
// #define URHO3D_PIXEL_NEED_BACKGROUND_COLOR

/// Whether to disable all lighting calculation.
// #define UNLIT

/// Whether to discard pixels with alpha less than 0.5 in diffuse texture. Material properties are ignored.
// #define ALPHAMASK

/// Whether to apply normal mapping.
// #define NORMALMAP

/// Whether to treat emission map as occlusion map.
// #define AO

/// Whether to sample reflections from environment cubemap.
// #define ENVCUBEMAP

/// Whether to use two-sided ligthing for geometries.
// #define TRANSLUCENT

/// Whether to use volumetric ligthing for geometries (forward lighting only).
// #define VOLUMETRIC

/// Whether to use physiclally based material.
// #define PBR

/// Whether to apply soft fade-out if URHO3D_SOFT_PARTICLES_ENABLED is defined.
// #define PARTICLE

/// Whether to render transparent object.
/// If defined, specular is not affected by alpha.
/// If not defined, object fades out completely when alpha approaches zero.
// #define TRANSPARENT

/// =================================== Deprecated external defines ===================================

// #define NOUV
// #define AMBIENT
// #define DEFERRED
// #define METALLIC
// #define ROUGHNESS

/// =================================== Global configuration ===================================

/// URHO3D_VERTEX_SHADER: Defined for vertex shader only.
#ifdef COMPILEVS
    #define URHO3D_VERTEX_SHADER
#endif

/// URHO3D_PIXEL_SHADER: Defined for pixel shader only.
#ifdef COMPILEPS
    #define URHO3D_PIXEL_SHADER
#endif

/// URHO3D_DEPTH_ONLY: Whether there's no color output and caller needs only depth.
#if defined(URHO3D_SHADOW_PASS)
    #define URHO3D_DEPTH_ONLY
#endif

/// URHO3D_NUM_RENDER_TARGETS: Number of active render targets.
#ifndef URHO3D_NUM_RENDER_TARGETS
    #if defined(URHO3D_GBUFFER_PASS)
        #define URHO3D_NUM_RENDER_TARGETS 4
    #else
        #define URHO3D_NUM_RENDER_TARGETS 1
    #endif
#endif

/// URHO3D_IS_LIT: Whether there's lighting in any form applied to geometry.
/// URHO3D_HAS_PIXEL_LIGHT: Whether there's per-pixel lighting from directional, point or spot light source.
/// URHO3D_NORMAL_MAPPING: Whether to apply normal mapping.
/// URHO3D_PIXEL_NEED_AMBIENT: Whether pixel shader needs ambient lighting.
#if !defined(UNLIT) && !defined(URHO3D_DEPTH_ONLY)
    #define URHO3D_IS_LIT

    #if defined(URHO3D_LIGHT_DIRECTIONAL) || defined(URHO3D_LIGHT_POINT) || defined(URHO3D_LIGHT_SPOT)
        #define URHO3D_HAS_PIXEL_LIGHT
    #endif

    #ifdef NORMALMAP
        #define URHO3D_NORMAL_MAPPING
    #endif

    #ifdef URHO3D_AMBIENT_PASS
        #define URHO3D_PIXEL_NEED_AMBIENT
    #endif
#endif

/// URHO3D_PHYSICAL_MATERIAL: Whether to use physiclally based material.
/// Implies URHO3D_LIGHT_HAS_SPECULAR.
/// Implies URHO3D_REFLECTION_MAPPING.
/// Changes semantics of specular texture:
/// - sSpecMap.r: Roughness;
/// - sSpecMap.g: Metallic factor;
/// - sSpecMap.a: Occlusion.
#if defined(URHO3D_IS_LIT) && defined(PBR)
    #define URHO3D_PHYSICAL_MATERIAL
    #ifndef URHO3D_LIGHT_HAS_SPECULAR
        #define URHO3D_LIGHT_HAS_SPECULAR
    #endif
#endif

/// URHO3D_PREMULTIPLY_ALPHA: Whether to pre-multiply alpha into output color
#if defined(URHO3D_IS_LIT) && defined(TRANSPARENT)
    #define URHO3D_PREMULTIPLY_ALPHA
#endif

/// URHO3D_REFLECTION_MAPPING: Whether to apply reflections from environment cubemap.
#if !defined(URHO3D_ADDITIVE_LIGHT_PASS) && (defined(ENVCUBEMAP) || defined(URHO3D_PHYSICAL_MATERIAL))
    #define URHO3D_REFLECTION_MAPPING
#endif

/// URHO3D_SOFT_PARTICLES: Whether to apply soft particles fade out.
#if defined(PARTICLE) && defined(URHO3D_SOFT_PARTICLES_ENABLED)
    #define URHO3D_SOFT_PARTICLES
#endif

/// URHO3D_SPECULAR_ANTIALIASING is disabled if derivatives are not supported.
#if defined(GL_ES) && !defined(GL_OES_standard_derivatives)
    #undef URHO3D_SPECULAR_ANTIALIASING
#endif

/// URHO3D_PIXEL_NEED_BACKGROUND_DEPTH is implied by soft particles
#ifdef URHO3D_SOFT_PARTICLES
    #ifndef URHO3D_PIXEL_NEED_BACKGROUND_DEPTH
        #define URHO3D_PIXEL_NEED_BACKGROUND_DEPTH
    #endif
#endif

// =================================== Vertex output configuration ===================================

/// URHO3D_PIXEL_NEED_EYE_VECTOR is implied by:
/// - Specular lighting;
/// - Reflection mapping.
#if defined(URHO3D_REFLECTION_MAPPING) || defined(URHO3D_LIGHT_HAS_SPECULAR)
    #ifndef URHO3D_PIXEL_NEED_EYE_VECTOR
        #define URHO3D_PIXEL_NEED_EYE_VECTOR
    #endif
#endif

/// TODO(renderer): Don't do it for particles?
/// URHO3D_PIXEL_NEED_NORMAL is implied by:
/// - Per-pixel lighting;
/// - Geometry buffer rendering for lit geometry;
/// - Reflection mapping.
#if defined(URHO3D_HAS_PIXEL_LIGHT) || (defined(URHO3D_IS_LIT) && defined(URHO3D_GBUFFER_PASS)) || defined(URHO3D_REFLECTION_MAPPING)
    #ifndef URHO3D_PIXEL_NEED_NORMAL
        #define URHO3D_PIXEL_NEED_NORMAL
    #endif
#endif

/// URHO3D_PIXEL_NEED_TANGENT is implied by normal mapping.
#if defined(URHO3D_NORMAL_MAPPING)
    #ifndef URHO3D_PIXEL_NEED_TANGENT
        #define URHO3D_PIXEL_NEED_TANGENT
    #endif
#endif

/// URHO3D_PIXEL_NEED_VERTEX_COLOR is implied by URHO3D_VERTEX_HAS_COLOR.
#if defined(URHO3D_VERTEX_HAS_COLOR) && !defined(URHO3D_SHADOW_PASS)
    #ifndef URHO3D_PIXEL_NEED_VERTEX_COLOR
        #define URHO3D_PIXEL_NEED_VERTEX_COLOR
    #endif
#endif

/// URHO3D_PIXEL_NEED_SCREEN_POSITION is implied by background color or depth.
#if defined(URHO3D_PIXEL_NEED_BACKGROUND_DEPTH) || defined(URHO3D_PIXEL_NEED_BACKGROUND_COLOR)
    #ifndef URHO3D_PIXEL_NEED_SCREEN_POSITION
        #define URHO3D_PIXEL_NEED_SCREEN_POSITION
    #endif
#endif

/// =================================== Vertex input configuration ===================================

#ifdef URHO3D_VERTEX_SHADER
    /// URHO3D_VERTEX_NORMAL_AVAILABLE: Whether vertex normal in object space is available.
    /// URHO3D_VERTEX_TANGENT_AVAILABLE: Whether vertex tangent in object space is available.
    /// Some geometry types may deduce vertex tangent or normal and don't need them present in actual vertex layout.
    /// Don't rely on URHO3D_VERTEX_HAS_NORMAL and URHO3D_VERTEX_HAS_TANGENT.
    #if defined(URHO3D_GEOMETRY_BILLBOARD) || defined(URHO3D_GEOMETRY_DIRBILLBOARD) || defined(URHO3D_GEOMETRY_TRAIL_FACE_CAMERA) || defined(URHO3D_GEOMETRY_TRAIL_BONE)
        #define URHO3D_VERTEX_NORMAL_AVAILABLE
        #define URHO3D_VERTEX_TANGENT_AVAILABLE
    #else
        #ifdef URHO3D_VERTEX_HAS_NORMAL
            #define URHO3D_VERTEX_NORMAL_AVAILABLE
        #endif
        #ifdef URHO3D_VERTEX_HAS_TANGENT
            #define URHO3D_VERTEX_TANGENT_AVAILABLE
        #endif
    #endif

    /// URHO3D_VERTEX_NEED_NORMAL is implied by:
    /// - URHO3D_PIXEL_NEED_NORMAL;
    /// - Any kind of lighting;
    /// - Shadow normal bias.
    #ifdef URHO3D_VERTEX_NORMAL_AVAILABLE
        #if defined(URHO3D_PIXEL_NEED_NORMAL) || defined(URHO3D_IS_LIT) || defined(URHO3D_SHADOW_NORMAL_OFFSET)
            #ifndef URHO3D_VERTEX_NEED_NORMAL
                #define URHO3D_VERTEX_NEED_NORMAL
            #endif
        #endif
    #endif

    /// URHO3D_VERTEX_NEED_TANGENT is implied by URHO3D_PIXEL_NEED_TANGENT.
    #ifdef URHO3D_VERTEX_TANGENT_AVAILABLE
        #if defined(URHO3D_PIXEL_NEED_TANGENT)
            #ifndef URHO3D_VERTEX_NEED_TANGENT
                #define URHO3D_VERTEX_NEED_TANGENT
            #endif
        #endif
    #endif

    /// URHO3D_SHADOW_NORMAL_OFFSET is disabled if vertex normal is not available.
    #if defined(URHO3D_SHADOW_NORMAL_OFFSET) && !defined(URHO3D_VERTEX_NORMAL_AVAILABLE)
        #undef URHO3D_SHADOW_NORMAL_OFFSET
    #endif
#endif // URHO3D_VERTEX_SHADER

/// =================================== Light configuration ===================================

#if defined(URHO3D_IS_LIT)
    // TODO(renderer): Handle this for pixel lights too
    /// URHO3D_SURFACE_ONE_SIDED: Normal is clamped when calculating lighting. Ignored in deferred rendering.
    /// URHO3D_SURFACE_TWO_SIDED: Normal is mirrored when calculating lighting. Ignored in deferred rendering.
    /// URHO3D_SURFACE_VOLUMETRIC: Normal is ignored when calculating lighting. Ignored in deferred rendering.
    /// VERTEX_ADJUST_NoL: Adjust N dot L for lighting in vertex shader.
    /// PIXEL_ADJUST_NoL: Adjust N dot L for lighting in pixel shader.
    #if defined(VOLUMETRIC)
        #define URHO3D_SURFACE_VOLUMETRIC
        #define VERTEX_ADJUST_NoL(NoL) 1.0
    #elif defined(TRANSLUCENT)
        #define URHO3D_SURFACE_TWO_SIDED
        #define VERTEX_ADJUST_NoL(NoL) abs(NoL)
    #else
        #define URHO3D_SURFACE_ONE_SIDED
        #define VERTEX_ADJUST_NoL(NoL) max(0.0, NoL)
    #endif
#endif

#if defined(URHO3D_HAS_SHADOW)
    // TODO(renderer): Revisit WebGL
    #if defined(URHO3D_LIGHT_DIRECTIONAL) && (!defined(GL_ES) || defined(WEBGL))
        #define URHO3D_SHADOW_NUM_CASCADES 4
    #else
        #define URHO3D_SHADOW_NUM_CASCADES 1
    #endif
#endif

/// =================================== Platform configuration ===================================

/// Make gl_FragData and gl_FragColor always accessible.
#ifdef URHO3D_PIXEL_SHADER
    #ifdef GL3
        out vec4 _fragData[URHO3D_NUM_RENDER_TARGETS];

        #define gl_FragColor _fragData[0]
        #define gl_FragData _fragData
    #else
        #if URHO3D_NUM_RENDER_TARGETS > 1
            #define gl_FragColor gl_FragData[0]
        #endif
    #endif
#endif

/// VERTEX_INPUT: Declare vertex input variable;
/// VERTEX_OUTPUT: Declare vertex output variable.
#if defined(URHO3D_VERTEX_SHADER)
    #ifdef GL3
        #define VERTEX_INPUT(decl) in decl;
        #define VERTEX_OUTPUT(decl) out decl;
    #else
        #define VERTEX_INPUT(decl) attribute decl;
        #define VERTEX_OUTPUT(decl) varying decl;
    #endif
#elif defined(URHO3D_PIXEL_SHADER)
    #ifdef GL3
        #define VERTEX_OUTPUT(decl) in decl;
    #else
        #define VERTEX_OUTPUT(decl) varying decl;
    #endif
#endif

/// UNIFORM_BUFFER_BEGIN: Begin of uniform buffer declaration;
/// UNIFORM_BUFFER_END: End of uniform buffer declaration;
/// UNIFORM: Declares scalar, vector or matrix uniform;
/// SAMPLER: Declares texture sampler.
#ifdef URHO3D_USE_CBUFFERS
    #ifdef GL_ARB_shading_language_420pack
        #define _URHO3D_LAYOUT(index) layout(binding=index)
    #else
        #define _URHO3D_LAYOUT(index)
    #endif

    #define UNIFORM_BUFFER_BEGIN(index, name) _URHO3D_LAYOUT(index) uniform name {
    #define UNIFORM(decl) decl;
    #define UNIFORM_BUFFER_END(index, name) };
    #define SAMPLER(index, decl) _URHO3D_LAYOUT(index) uniform decl;
#else
    #define UNIFORM_BUFFER_BEGIN(index, name)
    #define UNIFORM(decl) uniform decl;
    #define UNIFORM_BUFFER_END(index, name)
    #define SAMPLER(index, decl) uniform decl;
#endif

/// INSTANCE_BUFFER_BEGIN: Begin of uniform buffer or group of per-instance attributes
/// INSTANCE_BUFFER_END: End of uniform buffer or instance attributes
#ifdef URHO3D_VERTEX_SHADER
    #ifdef URHO3D_INSTANCING
        #define INSTANCE_BUFFER_BEGIN(index, name)
        #define INSTANCE_BUFFER_END(index, name)
    #else
        #define INSTANCE_BUFFER_BEGIN(index, name) UNIFORM_BUFFER_BEGIN(index, name)
        #define INSTANCE_BUFFER_END(index, name) UNIFORM_BUFFER_END(index, name)
    #endif
#endif

/// URHO3D_FLIP_FRAMEBUFFER: Whether Y axis of clip space is the opposite of Y axis of render target texture.
#ifdef D3D11
    #define URHO3D_FLIP_FRAMEBUFFER
#endif

/// ivec4_attrib: Compatible ivec4 vertex attribite. Cast to ivec4 before use.
#ifdef D3D11
    #define ivec4_attrib ivec4
#else
    #define ivec4_attrib vec4
#endif

// Compatible texture samplers for GL3
#ifdef GL3
    #define texture2D texture
    #define texture2DProj textureProj
    #define texture3D texture
    #define textureCube texture
    #define textureCubeLod textureLod
    #define texture2DLod textureLod
    #define texture2DLodOffset textureLodOffset
#endif

/// UNIFORM_HIGHP: Uniform with max precision, undefined if not supported.
/// SAMPLER_HIGHP: Sampler with max precision, mediump if not supported.
#ifndef GL_ES
    #define UNIFORM_HIGHP(decl) UNIFORM(decl)
    #define SAMPLER_HIGHP(index, decl) SAMPLER(index, decl)
#else
    /// Use max available precision by default
    #if defined(GL_FRAGMENT_PRECISION_HIGH) || !defined(URHO3D_PIXEL_SHADER)
        precision highp float;
        #define UNIFORM_HIGHP(decl) UNIFORM(decl)
        #define SAMPLER_HIGHP(index, decl) SAMPLER(index, highp decl)
    #else
        precision mediump float;
        #define UNIFORM_HIGHP(decl)
        #define SAMPLER_HIGHP(index, decl) SAMPLER(index, mediump decl)
    #endif
#endif

// TODO(renderer): Get rid of optional_highp
#define optional_highp

#define M_MEDIUMP_FLT_MAX 65504.0
#define SaturateMediump(x) min(x, M_MEDIUMP_FLT_MAX)

#endif // _CONFIG_GLSL_
