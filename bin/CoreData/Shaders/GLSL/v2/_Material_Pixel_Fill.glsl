/// _Material_Pixel_Fill.glsl
/// Don't include!
/// Fill SurfaceData structure.

/// Fill fog factor in fragment data.
/// out: SurfaceData.fogFactor
#define FillSurfaceFogFactor(surfaceData) \
    surfaceData.fogFactor = GetFogFactor(vWorldDepth)

/// Fill ambient lighting for fragment.
/// out: SurfaceData.ambientLighting
#ifdef URHO3D_SURFACE_NEED_AMBIENT
    half3 _GetFragmentAmbient()
    {
        half3 ambientLighting = vAmbientAndVertexLigthing;
    #ifdef URHO3D_HAS_LIGHTMAP
        ambientLighting += GammaToLightSpace(2.0 * texture2D(sEmissiveMap, vTexCoord2).rgb);
    #endif
        return ambientLighting;
    }

    #define FillSurfaceAmbient(surfaceData) \
        surfaceData.ambientLighting = _GetFragmentAmbient()
#else
    #define FillSurfaceAmbient(surfaceData)
#endif

/// Fill eye vector for fragment.
/// out: SurfaceData.eyeVec
#ifdef URHO3D_PIXEL_NEED_EYE_VECTOR
    #define FillSurfaceEyeVector(surfaceData) \
        surfaceData.eyeVec = normalize(vEyeVec)
#else
    #define FillSurfaceEyeVector(surfaceData)
#endif

/// Fill screen position for fragment.
/// out: SurfaceData.screenPos
#ifdef URHO3D_PIXEL_NEED_SCREEN_POSITION
    #define FillSurfaceScreenPosition(surfaceData) \
        surfaceData.screenPos = vScreenPos.xy / vScreenPos.w
#else
    #define FillSurfaceScreenPosition(surfaceData)
#endif

/// Fill normal for fragment.
/// out: SurfaceData.normal
/// out: SurfaceData.normalInTangentSpace
#ifdef URHO3D_SURFACE_NEED_NORMAL
    half3 _GetFragmentNormalEx(out half3 normalInTangentSpace)
    {
    #ifdef URHO3D_NORMAL_MAPPING
        normalInTangentSpace = DecodeNormal(texture2D(sNormalMap, vTexCoord));
        mediump mat3 tbn = mat3(vTangent.xyz, vec3(vBitangentXY.xy, vTangent.w), vNormal);
        half3 normal = normalize(tbn * normalInTangentSpace);
    #else
        normalInTangentSpace = vec3(0.0, 0.0, 1.0);
        half3 normal = normalize(vNormal);
    #endif

    #ifdef URHO3D_SURFACE_TWO_SIDED
        normal *= SELECT_FRONT_BACK_FACE(1.0, -1.0);
    #endif

        return normal;
    }

    half3 _GetFragmentNormal()
    {
        half3 normalInTangentSpace;
        return _GetFragmentNormalEx(normalInTangentSpace);
    }

    #ifdef URHO3D_SURFACE_NEED_NORMAL_IN_TANGENT_SPACE
        #define FillSurfaceNormal(surfaceData) \
            surfaceData.normal = _GetFragmentNormalEx(surfaceData.normalInTangentSpace)
    #else
        #define FillSurfaceNormal(surfaceData) \
            surfaceData.normal = _GetFragmentNormal()
    #endif
#else
    #ifdef URHO3D_SURFACE_NEED_NORMAL_IN_TANGENT_SPACE
        #error URHO3D_SURFACE_NEED_NORMAL_IN_TANGENT_SPACE requires URHO3D_SURFACE_NEED_NORMAL
    #else
        #define FillSurfaceNormal(surfaceData)
    #endif
#endif

/// Fill reflectivity (or metalness), roughness and occlusion for fragment.
/// out: SurfaceData.oneMinusReflectivity
/// out: SurfaceData.roughness
/// out: SurfaceData.occlusion
#if (URHO3D_SPECULAR == 2) && defined(URHO3D_FEATURE_DERIVATIVES) && defined(URHO3D_PHYSICAL_MATERIAL)
    half _GetAdjustedFragmentRoughness(const half roughness, const half3 normal)
    {
        half3 dNdx = dFdx(normal);
        half3 dNdy = dFdy(normal);
        const half specularAntiAliasingVariance = 0.15;
        const half specularAntiAliasingThreshold = 0.2;
        half variance = specularAntiAliasingVariance * max(dot(dNdx, dNdx), dot(dNdy, dNdy));

        half physicalRoughness = roughness * roughness;
        half kernelRoughness = min(2.0 * variance, specularAntiAliasingThreshold);
        half squareRoughness = min(1.0, physicalRoughness * physicalRoughness + kernelRoughness);
        return sqrt(sqrt(squareRoughness));
    }

    #define _AdjustFragmentRoughness(surfaceData) \
        surfaceData.roughness = _GetAdjustedFragmentRoughness(surfaceData.roughness, surfaceData.normal)
#else
    #define _AdjustFragmentRoughness(surfaceData)
#endif

#ifdef URHO3D_PHYSICAL_MATERIAL
    void _GetFragmentMetallicRoughnessOcclusion(out half oneMinusReflectivity, out half roughness, out half occlusion)
    {
    #ifdef URHO3D_MATERIAL_HAS_SPECULAR
        half3 rmo = texture2D(sSpecMap, vTexCoord).rga;
    #else
        half3 rmo = vec3(cRoughness, cMetallic, 1.0);
    #endif

        const half minRoughness = 0.089;
        half oneMinusDielectricReflectivity = 1.0 - 0.16 * cDielectricReflectance * cDielectricReflectance;

        roughness = max(rmo.x, minRoughness);
        oneMinusReflectivity = oneMinusDielectricReflectivity - oneMinusDielectricReflectivity * rmo.y;
        occlusion = rmo.z;
    }
#else
    void _GetFragmentMetallicRoughnessOcclusion(out half oneMinusReflectivity, out half roughness, out half occlusion)
    {
    // Consider non-PBR materials either non-reflective or 100% reflective
    #ifdef ENVCUBEMAP
        oneMinusReflectivity = 0.0;
    #else
        oneMinusReflectivity = 1.0;
    #endif

        roughness = 0.5;

    #if !defined(URHO3D_HAS_LIGHTMAP) && defined(AO) && defined(URHO3D_MATERIAL_HAS_EMISSIVE)
        occlusion = texture2D(sEmissiveMap, vTexCoord).r;
    #else
        occlusion = 1.0;
    #endif
    }
#endif

#define FillSurfaceMetallicRoughnessOcclusion(surfaceData) \
    _GetFragmentMetallicRoughnessOcclusion(surfaceData.oneMinusReflectivity, surfaceData.roughness, surfaceData.occlusion); \
    _AdjustFragmentRoughness(surfaceData)

/// Fill surface albedo and specular.
/// out: SurfaceData.albedo
/// out: SurfaceData.specular
void _GetFragmentAlbedoSpecular(const half oneMinusReflectivity, out half4 albedo, out half3 specular)
{
#ifdef URHO3D_MATERIAL_HAS_DIFFUSE
    half4 albedoInput = texture2D(sDiffMap, vTexCoord);
    #ifdef ALPHAMASK
        if (albedoInput.a < 0.5)
            discard;
    #endif
    albedo = cMatDiffColor * DiffMap_ToLight(albedoInput);
#else
    albedo = cMatDiffColor;
#endif

#ifdef URHO3D_PIXEL_NEED_VERTEX_COLOR
    albedo *= vColor;
#endif

#ifndef URHO3D_MATERIAL_HAS_DIFFUSE
    albedo = GammaToLightSpaceAlpha(albedo);
#endif

#ifdef URHO3D_PHYSICAL_MATERIAL
    specular = albedo.rgb * (1.0 - oneMinusReflectivity);
    albedo.rgb *= oneMinusReflectivity;
#else
    #ifdef URHO3D_MATERIAL_HAS_SPECULAR
        specular = cMatSpecColor.rgb * texture2D(sSpecMap, vTexCoord).rgb;
    #else
        specular = cMatSpecColor.rgb;
    #endif
#endif

#ifdef URHO3D_PREMULTIPLY_ALPHA
    albedo.rgb *= albedo.a;
    #ifdef URHO3D_PHYSICAL_MATERIAL
        albedo.a = 1.0 - oneMinusReflectivity + albedo.a * oneMinusReflectivity;
    #endif
#endif
}

#define FillSurfaceAlbedoSpecular(surfaceData) \
    _GetFragmentAlbedoSpecular(surfaceData.oneMinusReflectivity, surfaceData.albedo, surfaceData.specular)

/// Fill surface emission.
/// out: SurfaceData.emission
#ifdef URHO3D_SURFACE_NEED_AMBIENT
    #ifndef URHO3D_HAS_LIGHTMAP
        #if defined(URHO3D_MATERIAL_HAS_EMISSIVE) && !defined(AO)
            #define FillSurfaceEmission(surfaceData) \
                surfaceData.emission = EmissiveMap_ToLight(cMatEmissiveColor * texture2D(sEmissiveMap, vTexCoord).rgb)
        #else
            #define FillSurfaceEmission(surfaceData) \
                surfaceData.emission = GammaToLightSpace(cMatEmissiveColor)
        #endif
    #else
        #define FillSurfaceEmission(surfaceData) \
            surfaceData.emission = vec3(0.0)
    #endif
#else
    #define FillSurfaceEmission(surfaceData)
#endif

/// Fill surface reflection color.
/// out: SurfaceData.reflectionColor
#ifdef URHO3D_SURFACE_NEED_REFLECTION_COLOR
    #ifdef URHO3D_MATERIAL_HAS_PLANAR_ENVIRONMENT
        half4 SamplePlanarReflectionColor(const vec2 screenPos, const half3 normal)
        {
            return texture2D(sEnvMap, GetPlanarReflectionUV(screenPos, vec4(normal, 1.0)));
        }

        #define FillSurfaceReflectionColor(surfaceData) \
            surfaceData.reflectionColor = SamplePlanarReflectionColor(surfaceData.screenPos, surfaceData.normal)
    #else
        half4 SampleCubeReflectionColor(const half3 normal, const half3 eyeVec, const half roughness)
        {
        #ifdef URHO3D_BLUR_REFLECTION
            half3 reflectionVec = reflect(-eyeVec, normal);
            return textureCubeLod(sEnvCubeMap, reflectionVec, roughness * cRoughnessToLODFactor);
        #else
            #ifdef URHO3D_VERTEX_REFLECTION
                return textureCube(sEnvCubeMap, vReflectionVec);
            #else
                half3 reflectionVec = reflect(-eyeVec, normal);
                return textureCube(sEnvCubeMap, reflectionVec);
            #endif
        #endif
        }

        #define FillSurfaceReflectionColor(surfaceData) \
            surfaceData.reflectionColor = SampleCubeReflectionColor(surfaceData.normal, surfaceData.eyeVec, surfaceData.roughness)
    #endif
#else
    #define FillSurfaceReflectionColor(surfaceData)
#endif

/// Fill surface background color.
/// out: SurfaceData.backgroundColor
#ifdef URHO3D_SURFACE_NEED_BACKGROUND_COLOR
    #define FillSurfaceBackgroundColor(surfaceData) \
        surfaceData.backgroundColor = texture2D(sEmissiveMap, surfaceData.screenPos).rgb
#else
    #define FillSurfaceBackgroundColor(surfaceData)
#endif

/// Fill surface background depth.
/// out: SurfaceData.backgroundDepth
#ifdef URHO3D_SURFACE_NEED_BACKGROUND_DEPTH
    #define FillSurfaceBackgroundDepth(surfaceData) \
        surfaceData.backgroundDepth = ReconstructDepth(texture2D(sDepthBuffer, surfaceData.screenPos).r)
#else
    #define FillSurfaceBackgroundDepth(surfaceData)
#endif

/// Fill common surface data not affected by material.
#define FillSurfaceCommon(surfaceData) \
    FillSurfaceFogFactor(surfaceData); \
    FillSurfaceAmbient(surfaceData); \
    FillSurfaceEyeVector(surfaceData); \
    FillSurfaceScreenPosition(surfaceData)

/// Fill surface background from samplers.
#define FillSurfaceBackground(surfaceData) \
    FillSurfaceBackgroundColor(surfaceData); \
    FillSurfaceBackgroundDepth(surfaceData)

