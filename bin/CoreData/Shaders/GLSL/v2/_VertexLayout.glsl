#ifndef _VERTEX_LAYOUT_GLSL_
#define _VERTEX_LAYOUT_GLSL_

#ifdef URHO3D_VERTEX_SHADER

#ifdef LAYOUT_HAS_POSITION
    VERTEX_INPUT(vec4 iPos)
#endif
#ifdef LAYOUT_HAS_NORMAL
    VERTEX_INPUT(vec3 iNormal)
#endif
#ifdef LAYOUT_HAS_COLOR
    VERTEX_INPUT(vec4 iColor)
#endif
#ifdef LAYOUT_HAS_TEXCOORD0
    VERTEX_INPUT(vec2 iTexCoord)
#endif
#ifdef LAYOUT_HAS_TEXCOORD1
    VERTEX_INPUT(vec2 iTexCoord1)
#endif
#ifdef LAYOUT_HAS_TANGENT
    VERTEX_INPUT(vec4 iTangent)
#endif
#ifdef GEOM_SKINNED
    VERTEX_INPUT(vec4 iBlendWeights)
    VERTEX_INPUT(ivec4_attrib iBlendIndices)
#endif
#ifdef GEOM_INSTANCED
    VERTEX_INPUT(vec4 iTexCoord4)
    VERTEX_INPUT(vec4 iTexCoord5)
    VERTEX_INPUT(vec4 iTexCoord6)
    #if defined(URHO3D_AMBIENT_DIRECTIONAL)
        VERTEX_INPUT(vec4 iTexCoord7)
        VERTEX_INPUT(vec4 iTexCoord8)
        VERTEX_INPUT(vec4 iTexCoord9)
        VERTEX_INPUT(vec4 iTexCoord10)
        VERTEX_INPUT(vec4 iTexCoord11)
        VERTEX_INPUT(vec4 iTexCoord12)
        VERTEX_INPUT(vec4 iTexCoord13)
    #elif defined(URHO3D_AMBIENT_FLAT)
        VERTEX_INPUT(vec4 iTexCoord7)
    #endif
#endif

#endif

#endif
