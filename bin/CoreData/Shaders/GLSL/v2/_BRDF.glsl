/// _BRDF.glsl
/// [Pixel Shader only]
/// [Lit material only]
/// Generalized functions used to calculate high-frequency surface lighting.
/// Independent from actual light types and properties.
/// Low-frequency lighting from spherical harmonics and lightmaps is not processed here.
#ifndef _BRDF_GLSL_
#define _BRDF_GLSL_

#ifdef URHO3D_PIXEL_SHADER

#ifdef URHO3D_IS_LIT

#ifndef _GAMMA_CORRECTION_GLSL_
    #error Include _GammaCorrection.glsl before _BRDF.glsl
#endif

/// Evaluate Schlick Fresnel function.
half3 F_Schlick(const half3 specularColor, const half u)
{
    half f = pow(1.0 - u, 5.0);
    return vec3(f) + specularColor * (vec3(1.0) - f);
}

#ifdef URHO3D_AMBIENT_PASS

/// Calculate simple indirect lighting for diffuse material.
half3 Indirect_Simple(const half3 ambientLighting, const half3 albedo, const half3 specular)
{
    return ambientLighting * albedo;
}

#ifdef URHO3D_REFLECTION_MAPPING
    /// Calculate simple indirect lighting for reflective material.
    half3 Indirect_SimpleReflection(const half3 ambientLighting, const half3 reflectionColor,
        const half3 albedo, const half3 specular)
    {
        return ambientLighting * albedo + GammaToLightSpace(specular * reflectionColor);
    }

    /// Calculate simple indirect lighting and transparency for water.
    half4 Indirect_SimpleWater(const half3 reflectionColor, const half3 specular, const half NoV)
    {
        half fresnel = pow(1.0 - NoV, 5.0);
        return vec4(GammaToLightSpace(specular * reflectionColor), fresnel);
    }
#endif

#ifdef URHO3D_PHYSICAL_MATERIAL

/// Evaluate approximated specular color for indirect lighting.
/// https://www.unrealengine.com/en-US/blog/physically-based-shading-on-mobile
half3 BRDF_IndirectSpecular(const half3 specularColor, const half roughness, const half NoV)
{
    const half4 c0 = vec4(-1, -0.0275, -0.572, 0.022);
    const half4 c1 = vec4(1, 0.0425, 1.04, -0.04);
    half4 r = roughness * c0 + c1;
    half a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    half2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return specularColor * ab.x + ab.y;
}

/// Calculate indirect PBR lighting.
half3 Indirect_PBR(const half3 ambientLighting, const half3 linearReflectionColor,
    const half3 albedo, const half3 specular, const half roughness, const half NoV)
{
#ifdef URHO3D_GAMMA_CORRECTION
    half3 brdf = BRDF_IndirectSpecular(specular, roughness, NoV);
#else
    half3 brdf = BRDF_IndirectSpecular(specular * specular, roughness, NoV);
#endif

    half3 specularColor = brdf * linearReflectionColor;
#ifndef URHO3D_GAMMA_CORRECTION
    specularColor = sqrt(max(specularColor, 0.0));
#endif

    return ambientLighting * albedo + specularColor;
}

/// Calculate indirect PBR lighting for transparent object.
half4 Indirect_PBRWater(const half3 reflectionColor, const half3 specular, const half NoV)
{
#ifdef URHO3D_GAMMA_CORRECTION
    half3 brdf = F_Schlick(specular, NoV);
#else
    half3 brdf = F_Schlick(specular * specular, NoV);
#endif

    half3 specularColor = brdf * GammaToLinearSpace(reflectionColor);

#ifndef URHO3D_GAMMA_CORRECTION
    specularColor = sqrt(max(specularColor, 0.0));
#endif

    return vec4(specularColor, brdf.x);
}

#endif // URHO3D_PHYSICAL_MATERIAL

#endif // URHO3D_AMBIENT_PASS

#ifdef URHO3D_LIGHT_PASS

/// Evaluate Blinn-Phong BRDF.
half BRDF_Direct_BlinnPhongSpecular(const half3 normal, const half3 halfVec, const half specularPower)
{
    return pow(max(dot(normal, halfVec), 0.0), specularPower);
}

/// Evaluate simple volumetric lighting for surface w/o normal.
half3 Direct_Volumetric(const half3 lightColor, const half3 albedo)
{
    return lightColor * albedo;
}

/// Evaluate simple directional lighting without specular.
half3 Direct_Simple(const half3 lightColor, const half3 albedo, const half3 lightVec, const half3 normal)
{
    half NoL = max(dot(normal, lightVec), 0.0);
    return NoL * lightColor * albedo;
}

/// Evaluate simple directional lighting with Blinn-Phong specular.
half3 Direct_SimpleSpecular(const half3 lightColor, const half3 albedo, const half3 specular,
    const half3 lightVec, const half3 normal, const half3 halfVec, const half specularPower, const half specularIntensity)
{
    half NoL = max(dot(normal, lightVec), 0.0);
    float brdf = BRDF_Direct_BlinnPhongSpecular(normal, halfVec, specularPower);
    return NoL * lightColor * (albedo + specular * (brdf * specularIntensity));
}

#ifdef URHO3D_PHYSICAL_MATERIAL

/// Evaluate GGX distribution function.
half D_GGX(const half roughness2, const half3 normal, const half3 halfVec, const half NoH)
{
#ifdef GL_ES
    half3 NxH = cross(normal, halfVec);
    half oneMinusNoHSquared = dot(NxH, NxH);
#else
    half oneMinusNoHSquared = 1.0 - NoH * NoH;
#endif

    half a = NoH * roughness2;
    half k = roughness2 / (oneMinusNoHSquared + a * a);
    // Don't divide by M_PI because we don't divide diffuse as well
    half d = k * k; // * (1.0 / M_PI);
    return SaturateMediump(d);
}

/// Evaluate Smith-GGX visibility function.
half V_SmithGGXCorrelatedFast(const half roughness2, const half NoV, const half NoL)
{
    half GGXV = NoL * (NoV * (1.0 - roughness2) + roughness2);
    half GGXL = NoV * (NoL * (1.0 - roughness2) + roughness2);
    return 0.5 / (GGXV + GGXL);
}

/// Calculate direct PBR lighting. Light attenuation is not applied.
half3 BRDF_Direct_PBRSpecular(const half3 specular, const half roughness,
    const half3 normal, const half3 halfVec, const half NoH, const half NoV, const half NoL, const half LoH)
{
    half roughness2 = roughness * roughness;
    half D = D_GGX(roughness2, normal, halfVec, NoH);
    half V = V_SmithGGXCorrelatedFast(roughness2, NoV, NoL);
    half3 F = F_Schlick(specular, LoH);
    return (D * V) * F;
}

/// Evaluate PBR lighting for direct light.
half3 Direct_PBR(const half3 lightColor, const half3 albedo, const half3 specular, const half roughness,
    const half3 lightVec, const half3 normal, const half3 eyeVec, const half3 halfVec)
{
    half NoL = max(dot(normal, lightVec), 0.0);
    half NoV = abs(dot(normal, eyeVec)) + 1e-5;
    half NoH = clamp(dot(normal, halfVec), 0.0, 1.0);
    half LoH = clamp(dot(lightVec, halfVec), 0.0, 1.0);

    half3 specularLight = BRDF_Direct_PBRSpecular(specular, roughness, normal, halfVec, NoH, NoV, NoL, LoH);
    return NoL * lightColor * (albedo + specularLight);
}

#endif // URHO3D_PHYSICAL_MATERIAL

#endif // URHO3D_LIGHT_PASS

#endif // URHO3D_IS_LIT

#endif // URHO3D_PIXEL_SHADER

#endif // _BRDF_GLSL_
