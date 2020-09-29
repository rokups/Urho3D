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

#endif
