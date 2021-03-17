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
    float fogFactor;
    /// Albedo color and alpha channel.
    vec4 albedo;
    /// Specular color.
    vec3 specular;
    /// Emission color.
    vec3 emission;
#ifdef URHO3D_AMBIENT_PASS
    /// Ambient lighting for surface, including global ambient, vertex lights and lightmaps.
    vec3 ambientLighting;
#endif
    /// Occlusion factor.
    float occlusion;
#ifdef URHO3D_PIXEL_NEED_NORMAL
    /// Normal.
    vec3 normal;
#endif
#ifdef URHO3D_PIXEL_NEED_EYE_VECTOR
    /// Vector from surface to eye in world space.
    vec3 eyeVec;
#endif
#ifdef URHO3D_REFLECTION_MAPPING
    /// Vector from surface to reflection.
    vec3 reflectionVec;
#endif
};

#endif // URHO3D_PIXEL_SHADER
#endif // _SURFACE_DATA_GLSL_
