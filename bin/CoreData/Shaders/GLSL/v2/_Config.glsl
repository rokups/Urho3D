#ifndef _CONFIG_GLSL_
#define _CONFIG_GLSL_

#extension GL_ARB_shading_language_420pack: enable

// =================================== Material configuration ===================================

// Ignored deprecated defines:
// NOUV: Detected automatically
// AMBIENT: Useless

// Material defines:
// NORMALMAP: Whether to apply normal mapping
// TRANSLUCENT: Whether to use two-sided ligthing for geometries
// VOLUMETRIC: Whether to use volumetric ligthing for geometries

// URHO3D_NORMAL_MAPPING: Whether to apply normal mapping
#if defined(NORMALMAP) && defined(URHO3D_VERTEX_HAS_NORMAL) && defined(URHO3D_VERTEX_HAS_TANGENT)
    #define URHO3D_NORMAL_MAPPING
#endif

// URHO3D_NEED_SECONDARY_TEXCOORD: Whether vertex needs second UV coordinate
#if defined(AO) || defined(URHO3D_HAS_LIGHTMAP) || defined(URHO3D_GEOMETRY_BILLBOARD) || defined(URHO3D_GEOMETRY_DIRBILLBOARD)
    #define URHO3D_NEED_SECONDARY_TEXCOORD
#endif

// URHO3D_SURFACE_ONE_SIDED: Normal is clamped when calculating lighting. Ignored in deferred rendering.
// URHO3D_SURFACE_TWO_SIDED: Normal is mirrored when calculating lighting. Ignored in deferred rendering.
// URHO3D_SURFACE_VOLUMETRIC: Normal is ignored when calculating lighting. Ignored in deferred rendering.
// CONVERT_N_DOT_L: Conversion function
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

// URHO3D_HAS_AMBIENT_LIGHT: Whether the shader need to process ambient and/or vertex lighting
#if defined(URHO3D_AMBIENT_PASS) || defined(URHO3D_NUM_VERTEX_LIGHTS)
    #define URHO3D_HAS_AMBIENT_LIGHT
#endif

// =================================== Platform configuration ===================================

// Helper utility to generate names
#define _CONCATENATE_2(x, y) x##y
#define CONCATENATE_2(x, y) _CONCATENATE_2(x, y)

// URHO3D_VERTEX_SHADER: Defined for vertex shader
// VERTEX_INPUT: Declare vertex input variable
// VERTEX_OUTPUT: Declare vertex output variable
#ifdef COMPILEVS
    #define URHO3D_VERTEX_SHADER

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
#ifdef COMPILEPS
    #define URHO3D_PIXEL_SHADER

    #ifdef GL3
        #define VERTEX_OUTPUT(decl) in decl;
    #else
        #define VERTEX_OUTPUT(decl) varying decl;
    #endif

    // Set default precicion to medium for GL ES pixel shader
    #ifdef GL_ES
        precision mediump float;
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
#endif

#endif // _CONFIG_GLSL_
