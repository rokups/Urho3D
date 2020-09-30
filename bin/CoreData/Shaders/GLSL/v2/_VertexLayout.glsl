#ifndef _VERTEX_LAYOUT_GLSL_
#define _VERTEX_LAYOUT_GLSL_

#ifdef STAGE_VERTEX_SHADER

#ifdef LAYOUT_HAS_POSITION
    VERTEX_SHADER_IN(vec4 iPos)
#endif
#ifdef LAYOUT_HAS_NORMAL
    VERTEX_SHADER_IN(vec3 iNormal)
#endif
#ifdef LAYOUT_HAS_COLOR
    VERTEX_SHADER_IN(vec4 iColor)
#endif
#ifdef LAYOUT_HAS_TEXCOORD0
    VERTEX_SHADER_IN(vec2 iTexCoord)
#endif
#ifdef LAYOUT_HAS_TEXCOORD1
    VERTEX_SHADER_IN(vec2 iTexCoord1)
#endif
#ifdef LAYOUT_HAS_TANGENT
    VERTEX_SHADER_IN(vec4 iTangent)
#endif
#ifdef GEOM_SKINNED
    VERTEX_SHADER_IN(vec4 iBlendWeights)
    VERTEX_SHADER_IN(ivec4_attrib iBlendIndices)
#endif
#ifdef GEOM_INSTANCED
    VERTEX_SHADER_IN(vec4 iTexCoord4)
    VERTEX_SHADER_IN(vec4 iTexCoord5)
    VERTEX_SHADER_IN(vec4 iTexCoord6)
    #ifdef SPHERICALHARMONICS
        VERTEX_SHADER_IN(vec4 iTexCoord7)
        VERTEX_SHADER_IN(vec4 iTexCoord8)
        VERTEX_SHADER_IN(vec4 iTexCoord9)
        VERTEX_SHADER_IN(vec4 iTexCoord10)
        VERTEX_SHADER_IN(vec4 iTexCoord11)
        VERTEX_SHADER_IN(vec4 iTexCoord12)
        VERTEX_SHADER_IN(vec4 iTexCoord13)
    #else
        VERTEX_SHADER_IN(vec4 iTexCoord7)
    #endif
#endif

#endif

#endif
