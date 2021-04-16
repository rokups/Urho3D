/// _Material.glsl
/// [No depth-only shaders]
/// Helpers used to create material shaders
#ifndef _MATERIAL_GLSL_
#define _MATERIAL_GLSL_

/// Common material includes
/// @{
#include "_Config.glsl"
#include "_GammaCorrection.glsl"
#include "_SurfaceData.glsl"
#include "_BRDF.glsl"

#include "_Uniforms.glsl"
#include "_Samplers.glsl"
#include "_VertexLayout.glsl"

#include "_VertexTransform.glsl"
#ifdef URHO3D_PIXEL_NEED_SCREEN_POSITION
#include "_VertexScreenPos.glsl"
#endif
#ifdef URHO3D_IS_LIT
#include "_IndirectLighting.glsl"
#include "_DirectLighting.glsl"
#include "_Shadow.glsl"
#endif
#include "_Fog.glsl"
/// @}

/// Common vertex output for any material
/// @{
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
/// @}

#ifdef URHO3D_VERTEX_SHADER

/// Fill common vertex shader outputs for material.
void FillCommonVertexOutput(VertexTransform vertexTransform, vec2 uv)
{
    gl_Position = WorldToClipSpace(vertexTransform.position.xyz);
    vWorldDepth = GetDepth(gl_Position);
    vTexCoord = uv;

#ifdef URHO3D_PIXEL_NEED_VERTEX_COLOR
    vColor = iColor;
#endif

#ifdef URHO3D_PIXEL_NEED_SCREEN_POSITION
    vScreenPos = GetScreenPos(gl_Position);
#endif

#ifdef URHO3D_PIXEL_NEED_NORMAL
    vNormal = vertexTransform.normal;
#endif

#ifdef URHO3D_PIXEL_NEED_TANGENT
    vTangent = cNormalScale * vec4(vertexTransform.tangent.xyz, vertexTransform.bitangent.z);
    vBitangentXY = cNormalScale * vertexTransform.bitangent.xy;
#endif

#ifdef URHO3D_PIXEL_NEED_EYE_VECTOR
    vEyeVec = cCameraPos - vertexTransform.position.xyz;
#endif

#ifdef URHO3D_PIXEL_NEED_AMBIENT
    vAmbientAndVertexLigthing = GetAmbientAndVertexLights(vertexTransform.position.xyz, vertexTransform.normal);
#endif

#ifdef URHO3D_IS_LIT

#ifdef URHO3D_HAS_LIGHTMAP
    vTexCoord2 = GetLightMapTexCoord();
#endif

#ifdef URHO3D_HAS_PIXEL_LIGHT
    vLightVec = GetLightVector(vertexTransform.position.xyz);
#endif

#ifdef URHO3D_HAS_SHADOW
    WorldSpaceToShadowCoord(vShadowPos, vertexTransform.position);
#endif

#ifdef URHO3D_LIGHT_CUSTOM_SHAPE
    vShapePos = vertexTransform.position * cLightShapeMatrix;
#endif

#endif // URHO3D_IS_LIT
}

#endif

#ifdef URHO3D_PIXEL_SHADER

/// Return FragmentData.
FragmentData GetFragmentData()
{
    FragmentData result;

    // Initialize fog factor
    result.fogFactor = GetFogFactor(vWorldDepth);

    // Initialize ambient lighting
#ifdef URHO3D_PIXEL_NEED_AMBIENT
    result.ambientLighting = vAmbientAndVertexLigthing;
    #ifdef URHO3D_HAS_LIGHTMAP
        result.ambientLighting += GammaToLightSpace(2.0 * texture2D(sEmissiveMap, vTexCoord2).rgb);
    #endif
#endif

    // Initialize eye vector
#ifdef URHO3D_PIXEL_NEED_EYE_VECTOR
    result.eyeVec = normalize(vEyeVec);
#endif

    // Initialize screen UV
#ifdef URHO3D_PIXEL_NEED_SCREEN_POSITION
    result.screenPos = vScreenPos.xy / vScreenPos.w;
#endif

    return result;
}

/// Return SurfaceGeometryData.
SurfaceGeometryData GetSurfaceGeometryData()
{
    SurfaceGeometryData result;

    // Initialize normal
#ifdef URHO3D_PIXEL_NEED_NORMAL
    #ifdef URHO3D_NORMAL_MAPPING
        mediump mat3 tbn = mat3(vTangent.xyz, vec3(vBitangentXY.xy, vTangent.w), vNormal);
        half3 normalInTangentSpace = DecodeNormal(texture2D(sNormalMap, vTexCoord));
        result.normal = normalize(tbn * normalInTangentSpace);

        #ifdef URHO3D_PIXEL_NEED_NORMAL_IN_TANGENT_SPACE
            result.normalInTangentSpace = normalInTangentSpace;
        #endif
    #else
        result.normal = normalize(vNormal);

        #ifdef URHO3D_PIXEL_NEED_NORMAL_IN_TANGENT_SPACE
            result.normalInTangentSpace = vec3(0.0, 0.0, 1.0);
        #endif
    #endif

    #ifdef URHO3D_SURFACE_TWO_SIDED
        result.normal *= gl_FrontFacing ? 1.0 : -1.0;
    #endif
#endif

    // Initialize roughness and reflectivity
#ifdef URHO3D_PHYSICAL_MATERIAL
    #ifdef URHO3D_MATERIAL_HAS_SPECULAR
        half3 rmo = texture2D(sSpecMap, vTexCoord).rga;
    #else
        half3 rmo = vec3(cRoughness, cMetallic, 1.0);
    #endif

    const half minRougness = 0.089;
    half oneMinusDielectricReflectivity = 1.0 - 0.16 * cDielectricReflectance * cDielectricReflectance;
    result.roughness = max(rmo.x, minRougness);
    result.oneMinusReflectivity = oneMinusDielectricReflectivity - oneMinusDielectricReflectivity * rmo.y;
#else
    // Consider non-PBR materials either non-reflective or 100% reflective
    #ifdef ENVCUBEMAP
        result.oneMinusReflectivity = 0.0;
    #else
        result.oneMinusReflectivity = 1.0;
    #endif
#endif

#if defined(URHO3D_SPECULAR_ANTIALIASING) && defined(URHO3D_PHYSICAL_MATERIAL)
    result.roughness = AdjustRoughness(result.roughness, result.normal);
#endif

    // Initialize occlusion
#ifndef URHO3D_ADDITIVE_LIGHT_PASS
    #ifdef URHO3D_PHYSICAL_MATERIAL
        result.occlusion = rmo.z;
    #elif !defined(URHO3D_HAS_LIGHTMAP) && defined(AO) && defined(URHO3D_MATERIAL_HAS_EMISSIVE)
        result.occlusion = texture2D(sEmissiveMap, vTexCoord).r;
    #else
        result.occlusion = 1.0;
    #endif
#endif

    return result;
}

/// Return SurfaceMaterialData.
SurfaceMaterialData GetSurfaceMaterialData(const half oneMinusReflectivity)
{
    SurfaceMaterialData result;

    // Initialize albedo
#ifdef URHO3D_MATERIAL_HAS_DIFFUSE
    half4 albedoInput = texture2D(sDiffMap, vTexCoord);
    #ifdef ALPHAMASK
        if (albedoInput.a < 0.5)
            discard;
    #endif
    result.albedo = cMatDiffColor * DiffMap_ToLight(albedoInput);
#else
    result.albedo = cMatDiffColor;
#endif

#ifdef URHO3D_PIXEL_NEED_VERTEX_COLOR
    result.albedo *= vColor;
#endif

#ifndef URHO3D_MATERIAL_HAS_DIFFUSE
    result.albedo = GammaToLightSpaceAlpha(result.albedo);
#endif

#ifdef URHO3D_IS_LIT
    // Initialize emission
    #ifndef URHO3D_HAS_LIGHTMAP
        #if defined(URHO3D_MATERIAL_HAS_EMISSIVE) && !defined(AO)
            result.emission = EmissiveMap_ToLight(cMatEmissiveColor.rgbb * texture2D(sEmissiveMap, vTexCoord)).rgb;
        #else
            result.emission = GammaToLightSpace(cMatEmissiveColor);
        #endif
    #else
        result.emission = vec3(0.0);
    #endif

    // Initialize specular
    #ifdef URHO3D_PHYSICAL_MATERIAL
        result.specular = result.albedo.rgb * (1.0 - oneMinusReflectivity);
        result.albedo.rgb *= oneMinusReflectivity;
    #else
        #ifdef URHO3D_MATERIAL_HAS_SPECULAR
            result.specular = cMatSpecColor.rgb * texture2D(sSpecMap, vTexCoord).rgb;
        #else
            result.specular = cMatSpecColor.rgb;
        #endif
    #endif
#endif

#ifdef URHO3D_PREMULTIPLY_ALPHA
    result.albedo.rgb *= result.albedo.a;
    #ifdef URHO3D_PHYSICAL_MATERIAL
        result.albedo.a = 1.0 - oneMinusReflectivity + result.albedo.a * oneMinusReflectivity;
    #endif
#endif

    return result;
}

/// Setup reflection data for fragment, if applicable.
#ifdef URHO3D_REFLECTION_MAPPING
    // Return reflection cubemap color as is.
    half4 SampleCubeReflectionColor(half3 normal, half3 eyeVec, half roughness)
    {
        half3 reflectionVec = reflect(-eyeVec, normal);
    #ifdef URHO3D_PHYSICAL_MATERIAL
        #ifdef GL_ES
            #ifdef GL_EXT_shader_texture_lod
                return textureCubeLodEXT(sEnvCubeMap, reflectionVec, roughness * cRoughnessToLODFactor);
            #else
                return textureCube(sEnvCubeMap, reflectionVec); // Too bad
            #endif
        #else
            return textureCubeLod(sEnvCubeMap, reflectionVec, roughness * cRoughnessToLODFactor);
        #endif
    #else
        return textureCube(sEnvCubeMap, reflectionVec);
    #endif
    }

    // Return reflection color taking eye vector and SurfaceGeometryData.
    #ifdef URHO3D_PHYSICAL_MATERIAL
        #define SetupFragmentReflectionColor(fragmentData, surfaceGeometryData) \
            fragmentData.reflectionColor = SampleCubeReflectionColor(surfaceGeometryData.normal, fragmentData.eyeVec, surfaceGeometryData.roughness)
    #else
        #define SetupFragmentReflectionColor(fragmentData, surfaceGeometryData) \
            fragmentData.reflectionColor = SampleCubeReflectionColor(surfaceGeometryData.normal, fragmentData.eyeVec, 0.0)
    #endif
#else
    #define SetupFragmentReflectionColor(fragmentData, surfaceGeometryData)
#endif

/// Setup background color data for fragment, if applicable.
#ifdef URHO3D_PIXEL_NEED_BACKGROUND_COLOR
    #define SetupFragmentBackgroundColor(fragmentData) \
        fragmentData.backgroundColor = texture2D(sEmissiveMap, fragmentData.screenPos).rgb
#else
    #define SetupFragmentBackgroundColor(fragmentData)
#endif

/// Setup background depth data for fragment, if applicable.
#ifdef URHO3D_PIXEL_NEED_BACKGROUND_DEPTH
    #define SetupFragmentBackgroundDepth(fragmentData) \
        fragmentData.backgroundDepth = ReconstructDepth(texture2D(sDepthBuffer, fragmentData.screenPos).r)
#else
    #define SetupFragmentBackgroundDepth(fragmentData)
#endif

/// Return final alpha with optionally applied fade out.
#ifdef URHO3D_SOFT_PARTICLES
    half GetSoftParticleFade(float fragmentDepth, float backgroundDepth)
    {
        // TODO(renderer): Make these configurable
        vec2 cMatParticleFadeParams = vec2(0.0, 1.0 * (cFarClip - cNearClip));
        float depthDelta = backgroundDepth - fragmentDepth - cMatParticleFadeParams.x;
        return clamp(depthDelta * cMatParticleFadeParams.y, 0.0, 1.0);
    }
    #define GetFinalAlpha(fragmentData, surfaceMaterialData) \
        surfaceMaterialData.albedo.a * GetSoftParticleFade(vWorldDepth, fragmentData.backgroundDepth)
#else
    #define GetFinalAlpha(fragmentData, surfaceMaterialData) surfaceMaterialData.albedo.a
#endif

#ifdef URHO3D_IS_LIT

#ifdef URHO3D_AMBIENT_PASS
    /// Calculate ambient lighting.
    half3 CalculateAmbientLighting(const FragmentData fragmentData,
        const SurfaceGeometryData surfaceGeometryData, const SurfaceMaterialData surfaceMaterialData)
    {
    #ifdef URHO3D_PHYSICAL_MATERIAL
        half NoV = abs(dot(surfaceGeometryData.normal, fragmentData.eyeVec)) + 1e-5;
        half3 diffuseAndSpecularColor = Indirect_PBR(
            fragmentData, surfaceMaterialData.albedo.rgb, surfaceMaterialData.specular,
            surfaceGeometryData.roughness, NoV);
    #else
        half3 diffuseAndSpecularColor = Indirect_Simple(
            fragmentData, surfaceMaterialData.albedo.rgb, cMatEnvMapColor);
    #endif
        return diffuseAndSpecularColor * surfaceGeometryData.occlusion + surfaceMaterialData.emission;
    }
#endif

#ifdef URHO3D_HAS_PIXEL_LIGHT
    /// Return pixel lighting data for forward rendering.
    DirectLightData GetForwardDirectLightData()
    {
        DirectLightData result;
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

    /// Calculate lighting from per-pixel light source.
    half3 CalculateDirectLighting(const FragmentData fragmentData,
        const SurfaceGeometryData surfaceGeometryData, const SurfaceMaterialData surfaceMaterialData)
    {
        DirectLightData lightData = GetForwardDirectLightData();

    #if defined(URHO3D_PHYSICAL_MATERIAL) || defined(URHO3D_LIGHT_HAS_SPECULAR)
        half3 halfVec = normalize(fragmentData.eyeVec + lightData.lightVec.xyz);
    #endif

    #if defined(URHO3D_SURFACE_VOLUMETRIC)
        half3 lightColor = Direct_Volumetric(lightData.lightColor, surfaceMaterialData.albedo.rgb);
    #elif defined(URHO3D_PHYSICAL_MATERIAL)
        half3 lightColor = Direct_PBR(lightData.lightColor, surfaceMaterialData.albedo.rgb,
            surfaceMaterialData.specular, surfaceGeometryData.roughness,
            lightData.lightVec.xyz, surfaceGeometryData.normal, fragmentData.eyeVec, halfVec);
    #elif defined(URHO3D_LIGHT_HAS_SPECULAR)
        half3 lightColor = Direct_SimpleSpecular(lightData.lightColor,
            surfaceMaterialData.albedo.rgb, surfaceMaterialData.specular,
            lightData.lightVec.xyz, surfaceGeometryData.normal, halfVec, cMatSpecColor.a, cLightColor.a);
    #else
        half3 lightColor = Direct_Simple(lightData.lightColor,
            surfaceMaterialData.albedo.rgb, lightData.lightVec.xyz, surfaceGeometryData.normal);
    #endif
        return lightColor * GetDirectLightAttenuation(lightData);
    }
#endif

/// Return color with applied lighting, but without fog.
/// Fills all channels of geometry buffer except destination color.
half3 GetFinalColor(const FragmentData fragmentData,
    const SurfaceGeometryData surfaceGeometryData, const SurfaceMaterialData surfaceMaterialData)
{
#ifdef URHO3D_AMBIENT_PASS
    vec3 finalColor = CalculateAmbientLighting(fragmentData, surfaceGeometryData, surfaceMaterialData);
#else
    vec3 finalColor = vec3(0.0);
#endif

#ifdef URHO3D_GBUFFER_PASS
    #ifdef URHO3D_PHYSICAL_MATERIAL
        float roughness = surfaceGeometryData.roughness;
    #else
        float roughness = 1.0 - cMatSpecColor.a / 255.0;
    #endif

    gl_FragData[1] = vec4(fragmentData.fogFactor * surfaceMaterialData.albedo.rgb, 0.0);
    gl_FragData[2] = vec4(fragmentData.fogFactor * surfaceMaterialData.specular, roughness);
    gl_FragData[3] = vec4(surfaceGeometryData.normal * 0.5 + 0.5, 0.0);
#elif defined(URHO3D_HAS_PIXEL_LIGHT)
    finalColor += CalculateDirectLighting(fragmentData, surfaceGeometryData, surfaceMaterialData);
#endif
    return finalColor;
}

#else // URHO3D_IS_LIT

/// Return color with optionally applied reflection, but without fog.
half3 GetFinalColor(const FragmentData fragmentData,
    const SurfaceGeometryData surfaceGeometryData, const SurfaceMaterialData surfaceMaterialData)
{
    vec3 finalColor = surfaceMaterialData.albedo.rgb;
#ifdef URHO3D_REFLECTION_MAPPING
    finalColor += GammaToLightSpace(cMatEnvMapColor * fragmentData.reflectionColor.rgb);
#endif

#ifdef URHO3D_GBUFFER_PASS
    gl_FragData[1] = vec4(0.0, 0.0, 0.0, 0.0);
    gl_FragData[2] = vec4(0.0, 0.0, 0.0, 0.0);
    gl_FragData[3] = vec4(0.5, 0.5, 0.5, 0.0);
#endif

    return finalColor;
}

#endif // URHO3D_IS_LIT

#endif // URHO3D_PIXEL_SHADER
#endif // _MATERIAL_GLSL_
