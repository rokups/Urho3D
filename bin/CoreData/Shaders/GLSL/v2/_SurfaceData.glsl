#ifndef _SURFACE_DATA_GLSL_
#define _SURFACE_DATA_GLSL_

#ifndef _CONFIG_GLSL_
    #error Include "_Config.glsl" before "_SurfaceData.glsl"
#endif

#ifdef URHO3D_PIXEL_SHADER

/// Common pixel input for any material.
struct SurfaceData
{
    /// Fog factor. 0 - fog color, 1 - material color.
    fixed fogFactor;
    /// Albedo color and alpha channel.
    half4 albedo;
    /// Specular color.
    half3 specular;
    /// Emission color.
    half3 emission;
#ifdef URHO3D_PHYSICAL_MATERIAL
    /// PBR roughness factor.
    half roughness;
#endif
#ifdef URHO3D_AMBIENT_PASS
    /// Ambient lighting for surface, including global ambient, vertex lights and lightmaps.
    half3 ambientLighting;
#endif
    /// Occlusion factor.
    half occlusion;
#ifdef URHO3D_PIXEL_NEED_NORMAL
    /// Normal.
    half3 normal;
#endif
#ifdef URHO3D_PIXEL_NEED_EYE_VECTOR
    /// Vector from surface to eye in world space.
    half3 eyeVec;
#endif
#ifdef URHO3D_REFLECTION_MAPPING
    /// Vector from surface to reflection.
    half3 reflectionVec;
    /// Reflection data from cubemap, prefetched.
    half4 reflectionColorRaw;
#endif
};

#endif // URHO3D_PIXEL_SHADER
#endif // _SURFACE_DATA_GLSL_
