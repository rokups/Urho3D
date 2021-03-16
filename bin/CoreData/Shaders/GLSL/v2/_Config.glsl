#ifndef _CONFIG_GLSL_
#define _CONFIG_GLSL_

#extension GL_ARB_shading_language_420pack: enable

// =================================== External defines ===================================

/// Whether vertex shader needs world-space normal.
// #define URHO3D_VERTEX_NEED_NORMAL

/// Whether vertex shader needs world-space tangent and bitangent.
// #define URHO3D_VERTEX_NEED_TANGENT

/// Whether pixel shader needs eye vector.
// #define URHO3D_PIXEL_NEED_EYE_VECTOR

/// Whether pixel shader needs world-space normal.
// #define URHO3D_PIXEL_NEED_NORMAL

/// Whether pixel shader needs world-space tangent and bitangent.
// #define URHO3D_PIXEL_NEED_TANGENT

/// Whether pixel shader needs vertex color.
// #define URHO3D_PIXEL_NEED_COLOR

/// Whether surface data in pixel shader needs specular.
// #define URHO3D_SURFACE_NEED_SPECULAR

/// Whether surface data in pixel shader needs occlusion.
// #define URHO3D_SURFACE_NEED_OCCLUSION

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

/// Whether to use two-sided or volumetric ligthing for geometries (forward lighting only);
// #define TRANSLUCENT
// #define VOLUMETRIC

// =================================== Deprecated external defines ===================================

// #define NOUV
// #define AMBIENT
// #define DEFERRED

// =================================== Global configuration ===================================

/// URHO3D_VERTEX_SHADER: Defined for vertex shader only.
#ifdef COMPILEVS
    #define URHO3D_VERTEX_SHADER
#endif

/// URHO3D_PIXEL_SHADER: Defined for pixel shader only.
#ifdef COMPILEPS
    #define URHO3D_PIXEL_SHADER
#endif

// URHO3D_IS_LIT: Whether there's lighting in any form applied to geometry.
// URHO3D_HAS_PIXEL_LIGHT: Whether there's per-pixel lighting from directional, point or spot light source.
// URHO3D_NORMAL_MAPPING: Whether to apply normal mapping.
#if !defined(UNLIT) && !defined(URHO3D_SHADOW_PASS)
    #define URHO3D_IS_LIT

    #if defined(URHO3D_LIGHT_DIRECTIONAL) || defined(URHO3D_LIGHT_POINT) || defined(URHO3D_LIGHT_SPOT)
        #define URHO3D_HAS_PIXEL_LIGHT
    #endif

    #ifdef NORMALMAP
        #define URHO3D_NORMAL_MAPPING
    #endif
#endif

// URHO3D_REFLECTION_MAPPING: Whether to apply reflections from environment cubemap
// TODO(renderer): Implement me
/*#if defined(URHO3D_AMBIENT_PASS) && defined(ENVCUBEMAP)
    #define URHO3D_REFLECTION_MAPPING
#endif*/

// =================================== Vertex output ===================================

#if defined(URHO3D_REFLECTION_MAPPING) || defined(URHO3D_LIGHT_HAS_SPECULAR)
    #ifndef URHO3D_PIXEL_NEED_EYE_VECTOR
        #define URHO3D_PIXEL_NEED_EYE_VECTOR
    #endif
#endif

// Request normal if has pixel lighting or if deferred
// TODO(renderer): Don't do it for particles?
#if defined(URHO3D_HAS_PIXEL_LIGHT) || (!defined(UNLIT) && defined(URHO3D_GBUFFER_PASS))
    #ifndef URHO3D_PIXEL_NEED_NORMAL
        #define URHO3D_PIXEL_NEED_NORMAL
    #endif
#endif

// Request tangent if normal mapping is enabled
#if defined(URHO3D_NORMAL_MAPPING)
    #ifndef URHO3D_PIXEL_NEED_TANGENT
        #define URHO3D_PIXEL_NEED_TANGENT
    #endif
#endif

// Request vertex color if not shadow pass
#if defined(URHO3D_VERTEX_HAS_COLOR) && !defined(URHO3D_SHADOW_PASS)
    #ifndef URHO3D_PIXEL_NEED_COLOR
        #define URHO3D_PIXEL_NEED_COLOR
    #endif
#endif

// =================================== Vertex input ===================================

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

    #ifdef URHO3D_VERTEX_NORMAL_AVAILABLE
        #if defined(URHO3D_PIXEL_NEED_NORMAL) || defined(URHO3D_IS_LIT) || defined(URHO3D_SHADOW_NORMAL_OFFSET)
            #ifndef URHO3D_VERTEX_NEED_NORMAL
                #define URHO3D_VERTEX_NEED_NORMAL
            #endif
        #endif
    #endif

    #ifdef URHO3D_VERTEX_TANGENT_AVAILABLE
        #if defined(URHO3D_PIXEL_NEED_TANGENT)
            #ifndef URHO3D_VERTEX_NEED_TANGENT
                #define URHO3D_VERTEX_NEED_TANGENT
            #endif
        #endif
    #endif

    // Disable shadow normal offset if there's no normal
    #if defined(URHO3D_SHADOW_NORMAL_OFFSET) && !defined(URHO3D_VERTEX_NORMAL_AVAILABLE)
        #undef URHO3D_SHADOW_NORMAL_OFFSET
    #endif
#endif // URHO3D_VERTEX_SHADER

// =================================== Light configuration ===================================

#if defined(URHO3D_IS_LIT)
    // URHO3D_SURFACE_ONE_SIDED: Normal is clamped when calculating lighting. Ignored in deferred rendering.
    // URHO3D_SURFACE_TWO_SIDED: Normal is mirrored when calculating lighting. Ignored in deferred rendering.
    // URHO3D_SURFACE_VOLUMETRIC: Normal is ignored when calculating lighting. Ignored in deferred rendering.
    // CONVERT_N_DOT_L: Conversion function.
    #if defined(VOLUMETRIC)
        #define URHO3D_SURFACE_VOLUMETRIC
        #define CONVERT_N_DOT_L(NdotL) 1.0
    #elif defined(TRANSLUCENT)
        #define URHO3D_SURFACE_TWO_SIDED
        #define CONVERT_N_DOT_L(NdotL) abs(NdotL)
    #else
        #define URHO3D_SURFACE_ONE_SIDED
        #define CONVERT_N_DOT_L(NdotL) max(0.0, NdotL)
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

// =================================== Platform configuration ===================================

// Helper utility to generate names
#define _CONCATENATE_2(x, y) x##y
#define CONCATENATE_2(x, y) _CONCATENATE_2(x, y)

// URHO3D_VERTEX_SHADER: Defined for vertex shader
// VERTEX_INPUT: Declare vertex input variable
// VERTEX_OUTPUT: Declare vertex output variable
#ifdef URHO3D_VERTEX_SHADER
    #ifdef GL3
        #define VERTEX_INPUT(decl) in decl;
        #define VERTEX_OUTPUT(decl) out decl;
    #else
        #define VERTEX_INPUT(decl) attribute decl;
        #define VERTEX_OUTPUT(decl) varying decl;
    #endif
#endif

// URHO3D_PIXEL_SHADER: Defined for pixel shader
// VERTEX_OUTPUT: Declared vertex shader output
#ifdef URHO3D_PIXEL_SHADER
    #ifdef GL3
        #define VERTEX_OUTPUT(decl) in decl;
    #else
        #define VERTEX_OUTPUT(decl) varying decl;
    #endif
#endif

// UNIFORM_BUFFER_BEGIN: Begin of uniform buffer declaration
// UNIFORM_BUFFER_END: End of uniform buffer declaration
// UNIFORM: Declares scalar, vector or matrix uniform
// SAMPLER: Declares texture sampler
#ifdef URHO3D_USE_CBUFFERS
    #ifdef GL_ARB_shading_language_420pack
        #define _URHO3D_LAYOUT(index) layout(binding=index)
    #else
        #define _URHO3D_LAYOUT(index)
    #endif

    #define UNIFORM_BUFFER_BEGIN(index, name) _URHO3D_LAYOUT(index) uniform name {
    #define UNIFORM(decl) decl;
    #define UNIFORM_BUFFER_END() };
    #define SAMPLER(index, decl) _URHO3D_LAYOUT(index) uniform decl;
#else
    #define UNIFORM_BUFFER_BEGIN(index, name)
    #define UNIFORM(decl) uniform decl;
    #define UNIFORM_BUFFER_END()
    #define SAMPLER(index, decl) uniform decl;
#endif

// INSTANCE_BUFFER_BEGIN: Begin of uniform buffer or group of per-instance attributes
// INSTANCE_BUFFER_END: End of uniform buffer or instance attributes
#ifdef URHO3D_VERTEX_SHADER
    #ifdef URHO3D_INSTANCING
        #define INSTANCE_BUFFER_BEGIN(index, name)
        #define INSTANCE_BUFFER_END()
    #else
        #define INSTANCE_BUFFER_BEGIN(index, name) UNIFORM_BUFFER_BEGIN(index, name)
        #define INSTANCE_BUFFER_END() UNIFORM_BUFFER_END()
    #endif
#endif

// UNIFORM_VERTEX: Declares uniform that is used only by vertex shader and may have high precision on GL ES
#if defined(GL_ES) && !defined(URHO3D_USE_CBUFFERS) && defined(URHO3D_PIXEL_SHADER)
    #define UNIFORM_VERTEX(decl)
#else
    #define UNIFORM_VERTEX(decl) UNIFORM(decl)
#endif

// URHO3D_FLIP_FRAMEBUFFER: Whether to flip framebuffer on rendering
#ifdef D3D11
    #define URHO3D_FLIP_FRAMEBUFFER
#endif

// ivec4_attrib: Compatible ivec4 vertex attribite. Cast to ivec4 before use.
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
    #define texture2DLod textureLod
    #define texture2DLodOffset textureLodOffset
#endif

// Disable precision modifiers if not GL ES
#ifndef GL_ES
    #define highp
    #define mediump
    #define lowp
#else
    #if defined(GL_FRAGMENT_PRECISION_HIGH) || !defined(URHO3D_PIXEL_SHADER)
        precision highp float;
    #else
        precision mediump float;
    #endif
#endif

// Define shortcuts for precision-qualified types
// TODO(renderer): Get rid of optional_highp
#define optional_highp

#endif // _CONFIG_GLSL_
