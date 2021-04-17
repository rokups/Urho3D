/// _Material_Common.glsl
/// Don't include!
/// Material attributes passed from vertex to pixel shader.

VERTEX_OUTPUT_HIGHP(float vWorldDepth)
VERTEX_OUTPUT_HIGHP(vec2 vTexCoord)

#ifdef URHO3D_PIXEL_NEED_VERTEX_COLOR
    VERTEX_OUTPUT(half4 vColor)
#endif

#ifdef URHO3D_PIXEL_NEED_SCREEN_POSITION
    VERTEX_OUTPUT_HIGHP(vec4 vScreenPos)
#endif

#ifdef URHO3D_PIXEL_NEED_NORMAL
    VERTEX_OUTPUT(half3 vNormal)
#endif

#ifdef URHO3D_PIXEL_NEED_TANGENT
    VERTEX_OUTPUT(half4 vTangent)
    VERTEX_OUTPUT(half2 vBitangentXY)
#endif

#ifdef URHO3D_PIXEL_NEED_EYE_VECTOR
    VERTEX_OUTPUT(half3 vEyeVec)
#endif

#ifdef URHO3D_PIXEL_NEED_AMBIENT
    VERTEX_OUTPUT(half3 vAmbientAndVertexLigthing)
#endif

#ifdef URHO3D_IS_LIT
    #ifdef URHO3D_HAS_LIGHTMAP
        VERTEX_OUTPUT_HIGHP(vec2 vTexCoord2)
    #endif

    #ifdef URHO3D_HAS_PIXEL_LIGHT
        VERTEX_OUTPUT(half3 vLightVec)
    #endif

    #ifdef URHO3D_HAS_SHADOW
        VERTEX_OUTPUT_HIGHP(vec4 vShadowPos[URHO3D_MAX_SHADOW_CASCADES])
    #endif

    #ifdef URHO3D_LIGHT_CUSTOM_SHAPE
        VERTEX_OUTPUT_HIGHP(vec4 vShapePos)
    #endif
#endif