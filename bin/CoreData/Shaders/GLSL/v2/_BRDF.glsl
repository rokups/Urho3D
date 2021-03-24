#ifndef _BRDF_GLSL_
#define _BRDF_GLSL_

#ifndef _GAMMA_CORRECTION_GLSL_
    #error Include "_GammaCorrection.glsl" before "_BRDF.glsl"
#endif

#ifdef URHO3D_PIXEL_SHADER

#ifdef URHO3D_AMBIENT_PASS

#ifndef _SURFACE_DATA_GLSL_
    #error Include "_SurfaceData.glsl" before "_BRDF.glsl"
#endif

/// Calculate simple indirect lighting: ambient and reflection. Also includes emission.
half3 Indirect_Simple(SurfaceData surfaceData)
{
    half3 result = surfaceData.emission;
    result += surfaceData.ambientLighting * surfaceData.albedo.rgb;
#ifdef URHO3D_REFLECTION_MAPPING
    result += GammaToLightSpace(cMatEnvMapColor * surfaceData.reflectionColorRaw.rgb);
#endif
    result *= surfaceData.occlusion;
    return result;
}

#ifdef URHO3D_PHYSICAL_MATERIAL

/// Evaluate approximated specular color for indirect lighting.
/// https://www.unrealengine.com/en-US/blog/physically-based-shading-on-mobile
half3 BRDF_IndirectSpecular(half3 specularColor, half roughness, half NoV)
{
    const half4 c0 = vec4(-1, -0.0275, -0.572, 0.022);
    const half4 c1 = vec4(1, 0.0425, 1.04, -0.04);
    half4 r = roughness * c0 + c1;
    half a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    half2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return specularColor * ab.x + ab.y;
}

/// Calculate indirect PBR lighting. Also includes emission.
half3 Indirect_PBR(SurfaceData surfaceData, half NoV)
{
#ifdef URHO3D_GAMMA_CORRECTION
    half3 brdf = BRDF_IndirectSpecular(surfaceData.specular, surfaceData.roughness, NoV);
#else
    half3 brdf = BRDF_IndirectSpecular(surfaceData.specular * surfaceData.specular, surfaceData.roughness, NoV);
#endif

    half3 diffuse = surfaceData.ambientLighting * surfaceData.albedo.rgb;
    half3 specular = brdf * GammaToLinearSpace(surfaceData.reflectionColorRaw.rgb);
#ifndef URHO3D_GAMMA_CORRECTION
    specular = sqrt(max(specular, 0.0));
#endif

    return surfaceData.emission + (diffuse + specular) * surfaceData.occlusion;
}

#endif // URHO3D_PHYSICAL_MATERIAL

#endif // URHO3D_AMBIENT_PASS

#ifdef URHO3D_HAS_PIXEL_LIGHT

/// Evaluate Blinn–Phong BRDF.
half BRDF_Direct_BlinnPhongSpecular(half3 normal, half3 halfVec, half specularPower)
{
    return pow(max(dot(normal, halfVec), 0.0), specularPower) * cLightColor.a;
}

/// Evaluate simple directional lighting without specular.
half3 Direct_Simple(half3 lightColor, half3 albedo, half3 lightVec, half3 normal)
{
    half NoL = max(dot(normal, lightVec), 0.0);
    return NoL * lightColor * albedo;
}

/// Evaluate simple directional lighting with Blinn-Phong specular.
half3 Direct_SimpleSpecular(half3 lightColor, half3 albedo, half3 specular,
    half3 lightVec, half3 normal, half3 halfVec, half specularPower)
{
    half NoL = max(dot(normal, lightVec), 0.0);
    float brdf = BRDF_Direct_BlinnPhongSpecular(normal, halfVec, specularPower);
    return NoL * lightColor * (albedo + specular * brdf);
}

#ifdef URHO3D_PHYSICAL_MATERIAL

/// Evaluate GGX distribution function.
half D_GGX(half roughness2, half3 normal, half3 halfVec, half NoH)
{
#ifdef GL_ES
    half3 NxH = cross(normal, halfVec);
    half oneMinusNoHSquared = dot(NxH, NxH);
#else
    half oneMinusNoHSquared = 1.0 - NoH * NoH;
#endif

    half a = NoH * roughness2;
    half k = roughness2 / (oneMinusNoHSquared + a * a);
    half d = k * k; // * (1.0 / M_PI); // Don't divide by M_PI because we don't divide diffuse as well
    return SaturateMediump(d);
}

/// Evaluate Smith-GGX visibility function.
half V_SmithGGXCorrelatedFast(half roughness2, half NoV, half NoL)
{
    half GGXV = NoL * (NoV * (1.0 - roughness2) + roughness2);
    half GGXL = NoV * (NoL * (1.0 - roughness2) + roughness2);
    return 0.5 / (GGXV + GGXL);
}

/// Evaluate Schlick Fresnel function.
half3 F_Schlick(half3 specularColor, half LoH)
{
    half f = pow(1.0 - LoH, 5.0);
    return vec3(f) + specularColor * (vec3(1.0) - f);
}

/// Calculate direct PBR lighting. Light attenuation is not applied.
half3 BRDF_Direct_PBRSpecular(half3 specular, half roughness,
    half3 normal, half3 halfVec, half NoH, half NoV, half NoL, half LoH)
{
    half roughness2 = roughness * roughness;
    half D = D_GGX(roughness2, normal, halfVec, NoH);
    half V = V_SmithGGXCorrelatedFast(roughness2, NoV, NoL);
    half3 F = F_Schlick(specular, LoH);
    return (D * V) * F;
}

/// Evaluate PBR lighting for direct light.
half3 Direct_PBR(half3 lightColor, half3 albedo, half3 specular, half roughness,
    half3 lightVec, half3 normal, half3 eyeVec, half3 halfVec)
{
    half NoL = max(dot(normal, lightVec), 0.0);
    half NoV = abs(dot(normal, eyeVec)) + 1e-5;
    half NoH = clamp(dot(normal, halfVec), 0.0, 1.0);
    half LoH = clamp(dot(lightVec, halfVec), 0.0, 1.0);

    half3 specularLight = BRDF_Direct_PBRSpecular(specular, roughness, normal, halfVec, NoH, NoV, NoL, LoH);
    return NoL * lightColor * (albedo + specularLight);
}

#endif // URHO3D_PHYSICAL_MATERIAL

#endif // URHO3D_HAS_PIXEL_LIGHT

#endif // URHO3D_PIXEL_SHADER

#endif // _BRDF_GLSL_
