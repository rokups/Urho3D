#ifndef _CONFIG_GLSL_
#define _CONFIG_GLSL_

#extension GL_ARB_shading_language_420pack: enable

// Vertex shader defines
#ifdef COMPILEVS
    #define STAGE_VERTEX_SHADER

    #ifdef GL3
        #define VERTEX_SHADER_IN(decl) in decl;
        #define VERTEX_SHADER_OUT(decl) out decl;
    #else
        #define VERTEX_SHADER_IN(decl) attribute decl;
        #define VERTEX_SHADER_OUT(decl) varying decl;
    #endif
#endif

// Pixel shader defines
#ifdef COMPILEPS
    #define STAGE_PIXEL_SHADER

    #ifdef GL3
        #define VERTEX_SHADER_OUT(decl) in decl;
    #else
        #define VERTEX_SHADER_OUT(decl) varying decl;
    #endif

    // Set default precicion to medium for GL ES pixel shader
    #ifdef GL_ES
        precision mediump float;
    #endif
#endif

// Helpers to declare shader constants
#ifdef URHO3D_USE_CBUFFERS
    #ifdef GL_ARB_shading_language_420pack
        #define CBUFFER_BEGIN(index, name) layout(binding=index) uniform name {
        #define CBUFFER_UNIFORM(decl) decl;
        #define CBUFFER_END() };
        #define UNIFORM_SAMPLER(index, decl) layout(binding=index) uniform decl;
    #else
        #define CBUFFER_BEGIN(index, name) uniform name {
        #define CBUFFER_UNIFORM(decl) decl;
        #define CBUFFER_END() };
        #define UNIFORM_SAMPLER(index, decl) uniform decl;
    #endif
#else
    #define CBUFFER_BEGIN(index, name)
    #define CBUFFER_UNIFORM(decl) uniform decl;
    #define CBUFFER_END()
    #define UNIFORM_SAMPLER(index, decl) uniform decl;
#endif

// Hint that attribute is used only by vertex shader (so it can be highp on GL ES)
// Ignored if not GL ES or if uniform buffers are used
#if defined(GL_ES) && !defined(URHO3D_USE_CBUFFERS) && defined(STAGE_PIXEL_SHADER)
    #define CBUFFER_UNIFORM_VS(decl)
#else
    #define CBUFFER_UNIFORM_VS(decl) CBUFFER_UNIFORM(decl)
#endif

// Compatible ivec4 vertex attribite
#ifdef D3D11
    #define ivec4_attrib ivec4
#else
    #define ivec4_attrib vec4
#endif

// Compatible texture sampling
#ifdef GL3
    #define texture2D texture
    #define texture2DProj textureProj
    #define texture3D texture
    #define textureCube texture
    #define texture2DLod textureLod
    #define texture2DLodOffset textureLodOffset
#endif

// Whether to flip framebuffer on rendering
#ifdef D3D11
    #define URHO3D_FLIP_FRAMEBUFFER
#endif

// Disable precision modifiers if not GL ES
#ifndef GL_ES
    #define highp
    #define mediump
    #define lowp
#endif

#endif
