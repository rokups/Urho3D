#ifndef _BRDF_GLSL_
#define _BRDF_GLSL_

#ifndef _SURFACE_DATA_GLSL_
    #error Include "_SurfaceData.glsl" before "_BRDF.glsl"
#endif

#ifdef URHO3D_PIXEL_SHADER

#ifdef URHO3D_AMBIENT_PASS

/// Calculate indirect lighting: material ambient and reflection.
vec3 BRDF_Indirect_Simple(SurfaceData surfaceData)
{
    vec3 result = surfaceData.emission;
    result += surfaceData.ambientLighting * surfaceData.albedo.rgb * surfaceData.occlusion;
#ifdef URHO3D_REFLECTION_MAPPING
    result += cMatEnvMapColor * textureCube(sEnvCubeMap, surfaceData.reflectionVec).rgb;
#endif
    return result;
}

#endif // URHO3D_AMBIENT_PASS

#endif // URHO3D_PIXEL_SHADER

#endif // _BRDF_GLSL_
