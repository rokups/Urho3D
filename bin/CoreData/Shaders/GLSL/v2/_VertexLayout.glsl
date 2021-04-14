#ifndef _VERTEX_LAYOUT_GLSL_
#define _VERTEX_LAYOUT_GLSL_

#ifndef _CONFIG_GLSL_
    #error Include _Config.glsl before _VertexLayout.glsl
#endif

#ifdef URHO3D_VERTEX_SHADER

// Vertex position
VERTEX_INPUT(vec4 iPos)
#ifdef URHO3D_GEOMETRY_SKINNED
    VERTEX_INPUT(vec4 iBlendWeights)
    VERTEX_INPUT(ivec4_attrib iBlendIndices)
#endif

// Optional parameters
// TODO(renderer): move geometry checks to config
#if defined(URHO3D_VERTEX_HAS_TEXCOORD0)
    VERTEX_INPUT(vec2 iTexCoord)
#endif
#if defined(URHO3D_VERTEX_HAS_TEXCOORD1) || defined(URHO3D_GEOMETRY_BILLBOARD) || defined(URHO3D_GEOMETRY_DIRBILLBOARD)
    VERTEX_INPUT(vec2 iTexCoord1)
#endif
#if defined(URHO3D_VERTEX_HAS_COLOR)
    VERTEX_INPUT(vec4 iColor)
#endif
#if defined(URHO3D_VERTEX_HAS_NORMAL) || defined(URHO3D_GEOMETRY_DIRBILLBOARD)
    VERTEX_INPUT(vec3 iNormal)
#endif
#if defined(URHO3D_VERTEX_HAS_TANGENT) || defined(URHO3D_GEOMETRY_TRAIL_FACE_CAMERA) || defined(URHO3D_GEOMETRY_TRAIL_BONE)
    VERTEX_INPUT(vec4 iTangent)
#endif

#endif // URHO3D_VERTEX_SHADER

#endif // _VERTEX_LAYOUT_GLSL_
