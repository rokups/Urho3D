#ifndef _MATERIAL_GLSL_
#define _MATERIAL_GLSL_

/// Common material includes
/// @{
#include "_Config.glsl"
#include "_Uniforms.glsl"
#include "_Samplers.glsl"
#include "_VertexLayout.glsl"
#include "_PixelOutput.glsl"

#include "_VertexTransform.glsl"
#include "_GammaCorrection.glsl"
#include "_IndirectLighting.glsl"
#include "_Shadow.glsl"
#include "_DirectLighting.glsl"
#include "_Fog.glsl"
/// @}

/// Common vertex output for any material
/// @{
VERTEX_OUTPUT(float vWorldDepth)
VERTEX_OUTPUT(vec2 vTexCoord)

#ifdef URHO3D_HAS_LIGHTMAP
    VERTEX_OUTPUT(vec2 vTexCoord2)
#endif

#ifdef URHO3D_PIXEL_NEED_COLOR
    VERTEX_OUTPUT(vec4 vColor)
#endif

#ifdef URHO3D_PIXEL_NEED_NORMAL
    VERTEX_OUTPUT(vec3 vNormal)
#endif

#ifdef URHO3D_PIXEL_NEED_TANGENT
    VERTEX_OUTPUT(vec4 vTangent)
    VERTEX_OUTPUT(vec2 vBitangentXY)
#endif

#ifdef URHO3D_HAS_PIXEL_LIGHT
    VERTEX_OUTPUT(vec3 vLightVec)
#endif

#ifdef URHO3D_PIXEL_NEED_EYE_VECTOR
    VERTEX_OUTPUT(vec3 vEyeVec)
#endif

#ifdef URHO3D_AMBIENT_PASS
    VERTEX_OUTPUT(vec3 vAmbientAndVertexLigthing)
#endif

#ifdef URHO3D_HAS_SHADOW
    VERTEX_OUTPUT(optional_highp vec4 vShadowPos[URHO3D_SHADOW_NUM_CASCADES])
#endif

#ifdef URHO3D_LIGHT_CUSTOM_SHAPE
    VERTEX_OUTPUT(vec4 vShapePos)
#endif
/// @}

#ifdef URHO3D_VERTEX_SHADER

/// Fill common vertex shader outputs for material.
void FillCommonVertexOutput(VertexTransform vertexTransform, vec2 uv)
{
    gl_Position = WorldToClipSpace(vertexTransform.position.xyz);
    vWorldDepth = GetDepth(gl_Position);
    vTexCoord = uv;

#ifdef URHO3D_HAS_LIGHTMAP
    vTexCoord2 = GetLightMapTexCoord();
#endif

#ifdef URHO3D_PIXEL_NEED_COLOR
    vColor = iColor;
#endif

#ifdef URHO3D_PIXEL_NEED_NORMAL
    vNormal = vertexTransform.normal;
#endif

#ifdef URHO3D_PIXEL_NEED_TANGENT
    vTangent = vec4(vertexTransform.tangent.xyz, vertexTransform.bitangent.z);
    vBitangentXY = vertexTransform.bitangent.xy;
#endif

#ifdef URHO3D_HAS_PIXEL_LIGHT
    vLightVec = GetLightVector(vertexTransform.position.xyz);
#endif

#ifdef URHO3D_PIXEL_NEED_EYE_VECTOR
    vEyeVec = GetEyeVector(vertexTransform.position.xyz);
#endif

#ifdef URHO3D_AMBIENT_PASS
    vAmbientAndVertexLigthing = GetAmbientAndVertexLights(vertexTransform.position.xyz, vertexTransform.normal);
#endif

#ifdef URHO3D_HAS_SHADOW
    WorldSpaceToShadowCoord(vShadowPos, vertexTransform.position);
#endif

#ifdef URHO3D_LIGHT_CUSTOM_SHAPE
    vShapePos = vertexTransform.position * cLightShapeMatrix;
#endif
}

#endif

#ifdef URHO3D_PIXEL_SHADER

#if defined(URHO3D_LIGHT_HAS_SPECULAR) || defined(URHO3D_GBUFFER_PASS)
    #ifndef URHO3D_SURFACE_NEED_SPECULAR
        #define URHO3D_SURFACE_NEED_SPECULAR
    #endif
#endif

#if (!defined(URHO3D_HAS_LIGHTMAP) && defined(AO) && defined(URHO3D_MATERIAL_HAS_EMISSIVE)) || defined(URHO3D_GBUFFER_PASS)
    #ifndef URHO3D_SURFACE_NEED_OCCLUSION
        #define URHO3D_SURFACE_NEED_OCCLUSION
    #endif
#endif

/// Common pixel input for any material.
struct SurfaceData
{
    /// Fog factor. 0 - fog color, 1 - material color.
    float fogFactor;
    /// Albedo color and alpha channel.
    vec4 albedo;
    /// Emission color.
    vec3 emission;
#ifdef URHO3D_AMBIENT_PASS
    /// Ambient lighting for surface, including global ambient, vertex lights and lightmaps.
    vec3 ambientLighting;
#endif
#ifdef URHO3D_SURFACE_NEED_SPECULAR
    /// Specular color.
    vec3 specular;
#endif
#ifdef URHO3D_SURFACE_NEED_OCCLUSION
    /// Occlusion factor.
    float occlusion;
#endif
#ifdef URHO3D_PIXEL_NEED_NORMAL
    /// Normal.
    vec3 normal;
#endif
#ifdef URHO3D_PIXEL_NEED_EYE_VECTOR
    /// Vector from surface to eye in world space.
    vec3 eyeVec;
#endif
};

/// Return common surface data in pixel shader.
SurfaceData GetCommonSurfaceData()
{
    SurfaceData result;
    result.fogFactor = GetFogFactor(vWorldDepth);

    // Evaluate albedo
#ifdef URHO3D_MATERIAL_HAS_DIFFUSE
    vec4 albedoInput = texture2D(sDiffMap, vTexCoord);
    #ifdef ALPHAMASK
        if (albedoInput.a < 0.5)
            discard;
    #endif
    result.albedo = cMatDiffColor * albedoInput;
#else
    result.albedo = cMatDiffColor;
#endif

#ifdef URHO3D_PIXEL_NEED_COLOR
    result.albedo *= vColor;
#endif

#ifdef URHO3D_MATERIAL_HAS_DIFFUSE
    result.albedo = DiffMap_ToLight(result.albedo);
#else
    result.albedo = GammaToLightSpaceAlpha(result.albedo);
#endif

    // Evaluate emission
#if defined(URHO3D_MATERIAL_HAS_EMISSIVE) && !defined(AO) && !defined(URHO3D_HAS_LIGHTMAP)
    result.emission = cMatEmissiveColor * texture2D(sEmissiveMap, vTexCoord).rgb;
#else
    result.emission = cMatEmissiveColor;
#endif

    // Evaluate specular
#ifdef URHO3D_SURFACE_NEED_SPECULAR
    #ifdef URHO3D_MATERIAL_HAS_SPECULAR
        result.specular = cMatSpecColor.rgb * texture2D(sSpecMap, vTexCoord).rgb;
    #else
        result.specular = cMatSpecColor.rgb;
    #endif
#endif

    // Evaluate occlusion
#ifdef URHO3D_SURFACE_NEED_OCCLUSION
    #if !defined(URHO3D_HAS_LIGHTMAP) && defined(AO) && defined(URHO3D_MATERIAL_HAS_EMISSIVE)
        result.occlusion = texture2D(sEmissiveMap, vTexCoord).r;
    #else
        result.occlusion = 1.0;
    #endif
#endif

    // Evaluate normal
#ifdef URHO3D_PIXEL_NEED_NORMAL
    #ifdef URHO3D_NORMAL_MAPPING
        mat3 tbn = mat3(vTangent.xyz, vec3(vBitangentXY.xy, vTangent.w), vNormal);
        result.normal = normalize(tbn * DecodeNormal(texture2D(sNormalMap, vTexCoord)));
    #else
        result.normal = normalize(vNormal);
    #endif
#endif

    // Evaluate eye vector
#ifdef URHO3D_PIXEL_NEED_EYE_VECTOR
    result.eyeVec = normalize(vEyeVec);
#endif

    // Evaluate ambient lighting
#ifdef URHO3D_AMBIENT_PASS
    result.ambientLighting = vAmbientAndVertexLigthing;
    #ifdef URHO3D_HAS_LIGHTMAP
        result.ambientLighting += 2.0 * texture2D(sEmissiveMap, vTexCoord2).rgb;
    #endif
    #ifdef URHO3D_SURFACE_NEED_OCCLUSION
        result.ambientLighting *= result.occlusion;
    #endif
#endif

    return result;
}

#ifdef URHO3D_HAS_PIXEL_LIGHT
    /// Return pixel lighting data for forward rendering.
    PixelLightData GetForwardPixelLightData()
    {
        PixelLightData result;
        result.lightVec = NormalizeLightVector(vLightVec);
    #ifdef URHO3D_LIGHT_CUSTOM_SHAPE
        result.lightColor = GetLightColorFromShape(vShapePos);
    #else
        result.lightColor = GetLightColor(result.lightVec.xyz);
    #endif
    #ifdef URHO3D_HAS_SHADOW
        vec4 shadowUV = ShadowCoordToUV(vShadowPos, vWorldDepth);
        result.shadow = FadeShadow(SampleShadowFiltered(shadowUV), vWorldDepth);
    #endif
        return result;
    }
#endif

#ifdef URHO3D_AMBIENT_PASS
    /// Return ambient lighting.
    #define GetSurfaceAmbient(surfaceData) (surfaceData.ambientLighting * surfaceData.albedo.rgb + surfaceData.emission)
#endif

#endif // URHO3D_PIXEL_SHADER
#endif // _MATERIAL_GLSL_
