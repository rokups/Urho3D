/// _Material_Pixel_Evaluate.glsl
/// Don't include!

/// Return final alpha with optionally applied fade out.
#ifdef URHO3D_SOFT_PARTICLES
    half GetSoftParticleFade(const half fragmentDepth, const half backgroundDepth)
    {
        half depthDelta = backgroundDepth - fragmentDepth - cFadeOffsetScale.x;
        return clamp(depthDelta * cFadeOffsetScale.y, 0.0, 1.0);
    }
    #define GetFinalAlpha(surfaceData) \
        surfaceData.albedo.a * GetSoftParticleFade(vWorldDepth, surfaceData.backgroundDepth)
#else
    #define GetFinalAlpha(surfaceData) surfaceData.albedo.a
#endif

#ifdef URHO3D_IS_LIT

#ifdef URHO3D_AMBIENT_PASS
    /// Calculate ambient lighting.
    half3 CalculateAmbientLighting(const SurfaceData surfaceData)
    {
    #ifdef URHO3D_PHYSICAL_MATERIAL
        half NoV = abs(dot(surfaceData.normal, surfaceData.eyeVec)) + 1e-5;
        #ifdef URHO3D_BLUR_REFLECTION
            half3 linearReflectionColor = GammaToLinearSpace(surfaceData.reflectionColor.rgb);
        #else
            half3 linearReflectionColor = mix(GammaToLinearSpace(surfaceData.reflectionColor.rgb),
                cReflectionAverageColor, surfaceData.roughness * surfaceData.roughness);
        #endif
        half3 diffuseAndSpecularColor = Indirect_PBR(
            surfaceData.ambientLighting, linearReflectionColor,
            surfaceData.albedo.rgb, surfaceData.specular,
            surfaceData.roughness, NoV);
    #elif defined(URHO3D_REFLECTION_MAPPING)
        half3 diffuseAndSpecularColor = Indirect_SimpleReflection(
            surfaceData.ambientLighting, surfaceData.reflectionColor.rgb, surfaceData.albedo.rgb, cMatEnvMapColor);
    #else
        half3 diffuseAndSpecularColor = Indirect_Simple(
            surfaceData.ambientLighting, surfaceData.albedo.rgb, cMatEnvMapColor);
    #endif
        return diffuseAndSpecularColor * surfaceData.occlusion + surfaceData.emission;
    }
#endif

#ifdef URHO3D_LIGHT_PASS
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
    half3 CalculateDirectLighting(const SurfaceData surfaceData)
    {
        DirectLightData lightData = GetForwardDirectLightData();

    #if defined(URHO3D_PHYSICAL_MATERIAL) || URHO3D_SPECULAR > 0
        half3 halfVec = normalize(surfaceData.eyeVec + lightData.lightVec.xyz);
    #endif

    #if defined(URHO3D_SURFACE_VOLUMETRIC)
        half3 lightColor = Direct_Volumetric(lightData.lightColor, surfaceData.albedo.rgb);
    #elif defined(URHO3D_PHYSICAL_MATERIAL)
        half3 lightColor = Direct_PBR(lightData.lightColor, surfaceData.albedo.rgb,
            surfaceData.specular, surfaceData.roughness,
            lightData.lightVec.xyz, surfaceData.normal, surfaceData.eyeVec, halfVec);
    #elif URHO3D_SPECULAR > 0
        half3 lightColor = Direct_SimpleSpecular(lightData.lightColor,
            surfaceData.albedo.rgb, surfaceData.specular,
            lightData.lightVec.xyz, surfaceData.normal, halfVec, cMatSpecColor.a, cLightColor.a);
    #else
        half3 lightColor = Direct_Simple(lightData.lightColor,
            surfaceData.albedo.rgb, lightData.lightVec.xyz, surfaceData.normal);
    #endif
        return lightColor * GetDirectLightAttenuation(lightData);
    }
#endif

/// Return color with applied lighting, but without fog.
/// Fills all channels of geometry buffer except destination color.
half3 GetFinalColor(const SurfaceData surfaceData)
{
#ifdef URHO3D_AMBIENT_PASS
    half3 finalColor = CalculateAmbientLighting(surfaceData);
#else
    half3 finalColor = vec3(0.0);
#endif

#ifdef URHO3D_GBUFFER_PASS
    #ifdef URHO3D_PHYSICAL_MATERIAL
        half roughness = surfaceData.roughness;
    #else
        half roughness = 1.0 - cMatSpecColor.a / 255.0;
    #endif

    gl_FragData[1] = vec4(surfaceData.fogFactor * surfaceData.albedo.rgb, 0.0);
    gl_FragData[2] = vec4(surfaceData.fogFactor * surfaceData.specular, roughness);
    gl_FragData[3] = vec4(surfaceData.normal * 0.5 + 0.5, 0.0);
#elif defined(URHO3D_LIGHT_PASS)
    finalColor += CalculateDirectLighting(surfaceData);
#endif
    return finalColor;
}

#else // URHO3D_IS_LIT

/// Return color with optionally applied reflection, but without fog.
half3 GetFinalColor(const SurfaceData surfaceData)
{
    half3 finalColor = surfaceData.albedo.rgb;
#ifdef URHO3D_REFLECTION_MAPPING
    finalColor += GammaToLightSpace(cMatEnvMapColor * surfaceData.reflectionColor.rgb);
#endif

#ifdef URHO3D_GBUFFER_PASS
    gl_FragData[1] = vec4(0.0, 0.0, 0.0, 0.0);
    gl_FragData[2] = vec4(0.0, 0.0, 0.0, 0.0);
    gl_FragData[3] = vec4(0.5, 0.5, 0.5, 0.0);
#endif

    return finalColor;
}

#endif // URHO3D_IS_LIT
