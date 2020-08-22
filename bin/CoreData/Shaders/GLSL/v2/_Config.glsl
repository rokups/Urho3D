#ifndef _CONFIG_GLSL_
#define _CONFIG_GLSL_

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
    #define CBUFFER_BEGIN(name) uniform name {
    #define CBUFFER_UNIFORM(decl) decl;
    #define CBUFFER_END() };
#else
    #define CBUFFER_BEGIN(name)
    #define CBUFFER_UNIFORM(decl) uniform decl;
    #define CBUFFER_END()
#endif

#endif
