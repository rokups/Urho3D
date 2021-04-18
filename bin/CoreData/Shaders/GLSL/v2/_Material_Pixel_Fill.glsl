/// _Material_Pixel_Fill.glsl
/// Don't include!

/// Fill fog factor in fragment data:
/// - SurfaceData.fogFactor
#define FillFragmentFogFactor(surfaceData) \
    surfaceData.fogFactor = GetFogFactor(vWorldDepth)

/// Fill ambient lighting for fragment:
/// - SurfaceData.ambientLighting
#ifdef URHO3D_SURFACE_NEED_AMBIENT
    half3 _GetFragmentAmbient()
    {
        half3 ambientLighting = vAmbientAndVertexLigthing;
    #ifdef URHO3D_HAS_LIGHTMAP
        ambientLighting += GammaToLightSpace(2.0 * texture2D(sEmissiveMap, vTexCoord2).rgb);
    #endif
        return ambientLighting;
    }

    #define FillFragmentAmbient(surfaceData) \
        surfaceData.ambientLighting = _GetFragmentAmbient()
#else
    #define FillFragmentAmbient(surfaceData)
#endif

/// Fill eye vector for fragment:
/// - SurfaceData.eyeVec
#ifdef URHO3D_PIXEL_NEED_EYE_VECTOR
    #define FillFragmentEyeVector(surfaceData) \
        surfaceData.eyeVec = normalize(vEyeVec)
#else
    #define FillFragmentEyeVector(surfaceData)
#endif

/// Fill screen position for fragment:
/// - SurfaceData.screenPos
#ifdef URHO3D_PIXEL_NEED_SCREEN_POSITION
    #define FillFragmentScreenPosition(surfaceData) \
        surfaceData.screenPos = vScreenPos.xy / vScreenPos.w
#else
    #define FillFragmentScreenPosition(surfaceData)
#endif

/// Fill normal for fragment:
/// - SurfaceData.normal
/// - SurfaceData.normalInTangentSpace
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
        normal *= gl_FrontFacing ? 1.0 : -1.0;
    #endif

        return normal;
    }

    half3 _GetFragmentNormal()
    {
        half3 normalInTangentSpace;
        return _GetFragmentNormalEx(normalInTangentSpace);
    }

    #ifdef URHO3D_SURFACE_NEED_NORMAL_IN_TANGENT_SPACE
        #define FillFragmentNormal(surfaceData) \
            surfaceData.normal = _GetFragmentNormalEx(surfaceData.normalInTangentSpace)
    #else
        #define FillFragmentNormal(surfaceData) \
            surfaceData.normal = _GetFragmentNormal()
    #endif
#else
    #ifdef URHO3D_SURFACE_NEED_NORMAL_IN_TANGENT_SPACE
        #error URHO3D_SURFACE_NEED_NORMAL_IN_TANGENT_SPACE requires URHO3D_SURFACE_NEED_NORMAL
    #else
        #define FillFragmentNormal(surfaceData)
    #endif
#endif

/// Fill reflectivity (or metalness), roughness and occlusion for fragment:
/// - SurfaceData.oneMinusReflectivity
/// - SurfaceData.roughness
/// - SurfaceData.occlusion
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

#define FillFragmentMetallicRoughnessOcclusion(surfaceData) \
    _GetFragmentMetallicRoughnessOcclusion(surfaceData.oneMinusReflectivity, surfaceData.roughness, surfaceData.occlusion)

/// Adjust roughness if needed:
/// - SurfaceData.roughness
#if (URHO3D_SPECULAR == 2) && defined(URHO3D_FEATURE_DERIVATIVES) && defined(URHO3D_PHYSICAL_MATERIAL)
    half _AdjustFragmentRoughness(const half roughness, const half3 normal)
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

    #define AdjustFragmentRoughness(surfaceData) \
        surfaceData.roughness = _AdjustFragmentRoughness(surfaceData.roughness, surfaceData.normal)
#else
    #define AdjustFragmentRoughness(surfaceData)
#endif

/// Fill fragment albedo and specular.
/// - SurfaceData.albedo
/// - SurfaceData.specular
void _GetFragmentAlbedoSpecular(half oneMinusReflectivity, out half4 albedo, out half3 specular)
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

#define FillFragmentAlbedoSpecular(surfaceData) \
    _GetFragmentAlbedoSpecular(surfaceData.oneMinusReflectivity, surfaceData.albedo, surfaceData.specular)

/// Fill fragment emission:
/// - SurfaceData.emission
#ifdef URHO3D_SURFACE_NEED_EMISSION
    #ifndef URHO3D_HAS_LIGHTMAP
        #if defined(URHO3D_MATERIAL_HAS_EMISSIVE) && !defined(AO)
            #define FillFragmentEmission(surfaceData) \
                surfaceData.emission = EmissiveMap_ToLight(cMatEmissiveColor.rgbb * texture2D(sEmissiveMap, vTexCoord)).rgb
        #else
            #define FillFragmentEmission(surfaceData) \
                surfaceData.emission = GammaToLightSpace(cMatEmissiveColor)
        #endif
    #else
        #define FillFragmentEmission(surfaceData) \
            surfaceData.emission = vec3(0.0)
    #endif
#else
    #define FillFragmentEmission(surfaceData)
#endif

/// Fill fragment reflection color:
/// - SurfaceData.reflectionColor
#ifdef URHO3D_SURFACE_NEED_REFLECTION_COLOR
    half4 _GetReflectionColor(half3 normal, half3 eyeVec, half roughness)
    {
    #ifdef URHO3D_PIXEL_NEED_REFLECTION_VECTOR
        #define reflectionVec vReflectionVec
    #else
        half3 reflectionVec = reflect(-eyeVec, normal);
    #endif

    #ifdef URHO3D_PHYSICAL_MATERIAL
        #ifdef URHO3D_FEATURE_CUBEMAP_LOD
            return textureCubeLod(sEnvCubeMap, reflectionVec, roughness * cRoughnessToLODFactor);
        #else
            return textureCube(sEnvCubeMap, reflectionVec); // Too bad
        #endif
    #else
        return textureCube(sEnvCubeMap, reflectionVec);
    #endif
    }

    #define FillFragmentReflectionColor(fragmentData) \
        fragmentData.reflectionColor = _GetReflectionColor(fragmentData.normal, fragmentData.eyeVec, fragmentData.roughness)
#else
    #define FillFragmentReflectionColor(fragmentData)
#endif

/// Fill fragment background color:
/// - SurfaceData.backgroundColor
#ifdef URHO3D_SURFACE_NEED_BACKGROUND_COLOR
    #define FillFragmentBackgroundColor(fragmentData) \
        fragmentData.backgroundColor = texture2D(sEmissiveMap, fragmentData.screenPos).rgb
#else
    #define FillFragmentBackgroundColor(fragmentData)
#endif

/// Fill fragment background depth:
/// - SurfaceData.backgroundDepth
#ifdef URHO3D_SURFACE_NEED_BACKGROUND_DEPTH
    #define FillFragmentBackgroundDepth(fragmentData) \
        fragmentData.backgroundDepth = ReconstructDepth(texture2D(sDepthBuffer, fragmentData.screenPos).r)
#else
    #define FillFragmentBackgroundDepth(fragmentData)
#endif
