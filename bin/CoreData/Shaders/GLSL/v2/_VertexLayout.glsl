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

#endif

#endif
