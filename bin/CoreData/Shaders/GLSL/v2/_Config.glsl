#ifndef _CONFIG_GLSL_
#define _CONFIG_GLSL_

#extension GL_ARB_shading_language_420pack: enable

// URHO3D_AMBIENT_MODE: Defines used ambient lighting mode
#if URHO3D_AMBIENT_MODE == 0
    #define URHO3D_AMBIENT_CONSTANT
#elif URHO3D_AMBIENT_MODE == 1
    #define URHO3D_AMBIENT_FLAT
#elif URHO3D_AMBIENT_MODE == 2
    #define URHO3D_AMBIENT_DIRECTIONAL
#endif

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
        #define LAYOUT(index) layout(binding=index)
    #else
        #define LAYOUT(index)
    #endif

    #define UNIFORM_BUFFER_BEGIN(index, name) LAYOUT(index) uniform name {
    #define UNIFORM(decl) decl;
    #define UNIFORM_BUFFER_END() };
    #define SAMPLER(index, decl) LAYOUT(index) uniform decl;

    #undef LAYOUT
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

#endif
