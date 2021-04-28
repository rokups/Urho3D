#ifndef _VERTEX_LAYOUT_GLSL_
#define _VERTEX_LAYOUT_GLSL_

#ifndef _CONFIG_GLSL_
    #error Include _Config.glsl before _VertexLayout.glsl
#endif

#ifdef URHO3D_VERTEX_SHADER

// Vertex position
VERTEX_INPUT(vec4 iPos)
#ifdef URHO3D_VERTEX_HAS_BONE_WEIGHTS_AND_INDICES
    VERTEX_INPUT(vec4 iBlendWeights)
    VERTEX_INPUT(ivec4_attrib iBlendIndices)
#endif

// Optional parameters
#ifdef URHO3D_VERTEX_HAS_TEXCOORD0
    VERTEX_INPUT(vec2 iTexCoord)
#endif
#ifdef URHO3D_VERTEX_HAS_TEXCOORD1
    VERTEX_INPUT(vec2 iTexCoord1)
#endif
#ifdef URHO3D_VERTEX_HAS_COLOR
    VERTEX_INPUT(vec4 iColor)
#endif
#ifdef URHO3D_VERTEX_HAS_NORMAL
    VERTEX_INPUT(vec3 iNormal)
#endif
#ifdef URHO3D_VERTEX_HAS_TANGENT
    VERTEX_INPUT(vec4 iTangent)
#endif

#endif // URHO3D_VERTEX_SHADER

#endif // _VERTEX_LAYOUT_GLSL_
