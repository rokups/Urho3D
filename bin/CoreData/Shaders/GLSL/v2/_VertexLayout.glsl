#ifndef _VERTEX_LAYOUT_GLSL_
#define _VERTEX_LAYOUT_GLSL_

#ifdef STAGE_VERTEX_SHADER

VERTEX_SHADER_IN(vec4 iPos)
VERTEX_SHADER_IN(vec3 iNormal)
VERTEX_SHADER_IN(vec4 iColor)
VERTEX_SHADER_IN(vec2 iTexCoord)
VERTEX_SHADER_IN(vec2 iTexCoord1)
VERTEX_SHADER_IN(vec4 iTangent)
VERTEX_SHADER_IN(vec4 iBlendWeights)
VERTEX_SHADER_IN(vec4 iBlendIndices)
VERTEX_SHADER_IN(vec3 iCubeTexCoord)
VERTEX_SHADER_IN(vec4 iCubeTexCoord1)
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
VERTEX_SHADER_IN(float iObjectIndex)

#endif

#endif
