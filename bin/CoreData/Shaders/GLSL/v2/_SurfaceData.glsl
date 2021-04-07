/// _SurfaceData.glsl
/// [Pixel Shader only]
/// [No depth-only shaders]
/// Defines structures which contain common data used for lighting calculation and color evaluation.
/// Aggergates input from vertex shader and bound textures.
#ifndef _SURFACE_DATA_GLSL_
#define _SURFACE_DATA_GLSL_

#ifndef _CONFIG_GLSL_
    #error Include _Config.glsl before _SurfaceData.glsl
#endif

#ifdef URHO3D_PIXEL_SHADER
#ifndef URHO3D_DEPTH_ONLY

/// Apply specular antialiasing to roughness if supported.
half AdjustRoughness(const half roughness, const half3 normal)
{
#if !defined(GL_ES) || defined(GL_OES_standard_derivatives)
    half3 dNdx = dFdx(normal);
    half3 dNdy = dFdy(normal);
    const half specularAntiAliasingVariance = 0.15;
    const half specularAntiAliasingThreshold = 0.2;
    half variance = specularAntiAliasingVariance * max(dot(dNdx, dNdx), dot(dNdy, dNdy));

    half physicalRoughness = roughness * roughness;
    half kernelRoughness = min(2.0 * variance, specularAntiAliasingThreshold);
    half squareRoughness = min(1.0, physicalRoughness * physicalRoughness + kernelRoughness);
    return sqrt(sqrt(squareRoughness));
#else
    return roughness;
#endif
}

/// Fragment data independent from surface material.
struct FragmentData
{
    /// Fog factor. 0 - fog color, 1 - material color.
    fixed fogFactor;
#ifdef URHO3D_PIXEL_NEED_AMBIENT
    /// Ambient lighting for surface, including global ambient, vertex lights and lightmaps.
    half3 ambientLighting;
#endif
#ifdef URHO3D_PIXEL_NEED_EYE_VECTOR
    /// Vector from surface to eye in world space.
    half3 eyeVec;
#endif
#ifdef URHO3D_REFLECTION_MAPPING
    /// Reflection color, initialized afterwards.
    /// It kind of depends on material roughness and normal.
    half4 reflectionColor;
#endif
#ifdef URHO3D_PIXEL_NEED_SCREEN_POSITION
    /// UV of depth or color background buffer corresponding to this fragment.
    vec2 screenPos;
#endif
#ifdef URHO3D_PIXEL_NEED_BACKGROUND_COLOR
    /// Color of background object.
    half3 backgroundColor;
#endif
#ifdef URHO3D_PIXEL_NEED_BACKGROUND_DEPTH
    /// Depth of background object.
    float backgroundDepth;
#endif
};

/// Surface data that defines high-frequency geometry details.
struct SurfaceGeometryData
{
#ifdef URHO3D_PIXEL_NEED_NORMAL
    /// Normal in world space, with normal mapping applied.
    half3 normal;
#endif
#ifdef URHO3D_PIXEL_NEED_NORMAL_IN_TANGENT_SPACE
    /// Normal in tangent space. If there's no normal map, it's always equal to (0, 0, 1).
    half3 normalInTangentSpace;
#endif
#ifdef URHO3D_PHYSICAL_MATERIAL
    /// Perceptual roughness factor.
    half roughness;
#endif
    /// 1 minus reflectivity (metalness in case of PBR shading).
    half oneMinusReflectivity;
#ifndef URHO3D_ADDITIVE_LIGHT_PASS
    /// Occlusion factor. 0 - no ambient is received, 1 - full ambient is received.
    half occlusion;
#endif
};

/// Surface data that corresponds to material.
struct SurfaceMaterialData
{
    /// Albedo color and alpha channel.
    half4 albedo;

#ifdef URHO3D_IS_LIT
    /// Specular color.
    half3 specular;
    /// Emission color.
    half3 emission;
#endif
};

struct SurfaceData
{
    /// Fog factor. 0 - fog color, 1 - material color.
    fixed fogFactor;
    /// Albedo color and alpha channel.
    half4 albedo;

#ifdef URHO3D_IS_LIT
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
#endif // URHO3D_IS_LIT
};

#endif // !URHO3D_DEPTH_ONLY
#endif // URHO3D_PIXEL_SHADER
#endif // _SURFACE_DATA_GLSL_
