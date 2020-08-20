#ifndef _CONFIG_GLSL_
#define _CONFIG_GLSL_

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
