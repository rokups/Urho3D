/// _Material_Common.glsl
/// Don't include!
/// Material attributes passed from vertex to pixel shader.

/// Vertex transform attributes:
/// @{
VERTEX_OUTPUT_HIGHP(float vWorldDepth)

#ifdef URHO3D_PIXEL_NEED_NORMAL
    VERTEX_OUTPUT(half3 vNormal)
#endif

#ifdef URHO3D_PIXEL_NEED_TANGENT
    VERTEX_OUTPUT(half4 vTangent)
    VERTEX_OUTPUT(half2 vBitangentXY)
#endif
/// @}

/// Vertex texcoord attributes (also vertex color, for convinience):
/// @{
VERTEX_OUTPUT_HIGHP(vec2 vTexCoord)

#ifdef URHO3D_PIXEL_NEED_LIGHTMAP_UV
    VERTEX_OUTPUT_HIGHP(vec2 vTexCoord2)
#endif

#ifdef URHO3D_PIXEL_NEED_VERTEX_COLOR
    VERTEX_OUTPUT(half4 vColor)
#endif
/// @}

/// Vertex lighting attributes
/// @{
#ifdef URHO3D_SURFACE_NEED_AMBIENT
    VERTEX_OUTPUT(half3 vAmbientAndVertexLigthing)
#endif

#ifdef URHO3D_PIXEL_NEED_LIGHT_VECTOR
    VERTEX_OUTPUT(half3 vLightVec)
#endif

#ifdef URHO3D_PIXEL_NEED_SHADOW_POS
    VERTEX_OUTPUT_HIGHP(vec4 vShadowPos[URHO3D_MAX_SHADOW_CASCADES])
#endif

#ifdef URHO3D_PIXEL_NEED_LIGHT_SHAPE_POS
    VERTEX_OUTPUT_HIGHP(vec4 vShapePos)
#endif
/// @}

/// Ungrouped attributes
/// @{
#ifdef URHO3D_PIXEL_NEED_SCREEN_POSITION
    VERTEX_OUTPUT_HIGHP(vec4 vScreenPos)
#endif

#ifdef URHO3D_PIXEL_NEED_EYE_VECTOR
    VERTEX_OUTPUT(half3 vEyeVec)
#endif

#ifdef URHO3D_PIXEL_NEED_REFLECTION_VECTOR
    VERTEX_OUTPUT(half3 vReflectionVec)
#endif
/// @}

/// Calculate planar reflection vector.
#ifdef URHO3D_MATERIAL_HAS_PLANAR_ENVIRONMENT
    half2 GetPlanarReflectionUV(vec2 screenPos, half4 normal)
    {
        // Screen position y has different direction for DX and GL.
        // Flip reflection vertically unless it's already flipped.
        #ifndef URHO3D_FEATURE_FRAMEBUFFER_Y_INVERTED
            screenPos.y = 1.0 - screenPos.y;
        #endif

        // Apply normal distortion, don't undershoot Y
        screenPos.x += dot(cReflectionPlaneX, normal);
        screenPos.y += max(0.0, dot(cReflectionPlaneY, normal));
        return screenPos;
    }
#endif
