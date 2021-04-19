/// _Material_Pixel_SurfaceData.glsl
/// Don't include!
/// Input of pixel shader. Contains both data passed from vertex shader and data fetched by pixel shader.

struct SurfaceData
{
    /// Fog factor. 0 - fog color, 1 - material color.
    fixed fogFactor;

    /// Perceptual roughness factor (0.5 in case of non-PBR shading)
    half roughness;
    /// 1 minus reflectivity (metalness in case of PBR shading).
    half oneMinusReflectivity;
    /// Occlusion factor. 0 - no ambient is received, 1 - full ambient is received.
    half occlusion;

    /// Albedo color and alpha channel.
    half4 albedo;
    /// Specular color.
    half3 specular;

#ifdef URHO3D_SURFACE_NEED_AMBIENT
    /// Ambient lighting for surface, including global ambient, vertex lights and lightmaps.
    half3 ambientLighting;
#endif

#ifdef URHO3D_PIXEL_NEED_EYE_VECTOR
    /// Vector from surface to eye in world space.
    half3 eyeVec;
#endif

#ifdef URHO3D_PIXEL_NEED_SCREEN_POSITION
    /// UV of depth or color background buffer corresponding to this fragment.
    vec2 screenPos;
#endif

#ifdef URHO3D_SURFACE_NEED_NORMAL
    /// Normal in world space, with normal mapping applied.
    half3 normal;
#endif

#ifdef URHO3D_SURFACE_NEED_NORMAL_IN_TANGENT_SPACE
    /// Normal in tangent space. If there's no normal map, it's always equal to (0, 0, 1).
    half3 normalInTangentSpace;
#endif

#ifdef URHO3D_SURFACE_NEED_AMBIENT
    /// Emission color.
    half3 emission;
#endif

#ifdef URHO3D_SURFACE_NEED_REFLECTION_COLOR
    /// Reflection color.
    half4 reflectionColor;
#endif

#ifdef URHO3D_SURFACE_NEED_BACKGROUND_COLOR
    /// Color of background object.
    half3 backgroundColor;
#endif

#ifdef URHO3D_SURFACE_NEED_BACKGROUND_DEPTH
    /// Depth of background object.
    float backgroundDepth;
#endif
};
