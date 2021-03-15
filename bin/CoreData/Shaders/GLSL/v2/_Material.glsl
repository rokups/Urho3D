#ifndef _MATERIAL_GLSL_
#define _MATERIAL_GLSL_

#ifndef _SAMPLERS_GLSL_
    #error Include "_Samplers.glsl" before "_Material.glsl"
#endif

#ifndef _GAMMA_CORRECTION_GLSL_
    #error Include "_GammaCorrection.glsl" before "_Material.glsl"
#endif

#ifdef URHO3D_PIXEL_SHADER

/// Return input diffuse color in light space, optionally discards texel
#ifdef URHO3D_MATERIAL_HAS_DIFFUSE
    #ifdef ALPHAMASK
        vec4 GetMaterialAlbedo(vec2 uv)
        {
            vec4 value = texture2D(sDiffMap, uv);
            if (value.a < 0.5)
                discard;
            return DiffMap_ToLight(cMatDiffColor * value);
        }
    #else
        #define GetMaterialAlbedo(uv) DiffMap_ToLight(cMatDiffColor * texture2D(sDiffMap, (uv)));
    #endif
#else
    #define GetMaterialAlbedo(uv) GammaToLightSpaceAlpha(cMatDiffColor);
#endif

/// Return input specular color (non-PBR rendering)
#ifdef URHO3D_HAS_SPECULAR_HIGHLIGHTS
    #ifdef URHO3D_MATERIAL_HAS_SPECULAR
        #define GetMaterialSpecularColor(uv) (cMatSpecColor.rgb * texture2D(sSpecMap, (uv)).rgb)
    #else
        #define GetMaterialSpecularColor(uv) (cMatSpecColor.rgb)
    #endif
#endif

/// Return normal adjusted according to normal map.
#ifdef URHO3D_NORMAL_MAPPING
    vec3 GetMaterialNormal(vec2 uv, vec3 normal, vec4 tangent, vec2 bitangentXY)
    {
        mat3 tbn = mat3(tangent.xyz, vec3(bitangentXY.xy, tangent.w), normal);
        return normalize(tbn * DecodeNormal(texture2D(sNormalMap, uv)));
    }
#else
    #define GetMaterialNormal(normal) normalize(normal)
#endif

/// Return final ambient lighting.
#ifdef URHO3D_AMBIENT_PASS
    #if defined(URHO3D_HAS_LIGHTMAP)
        #define GetAmbientLightingFromLightmap(vertexLighting, uv2, albedo) \
            (((vertexLighting) + texture2D(sEmissiveMap, (uv2)).rgb * 2.0) * (albedo))
    #elif defined(AO) && defined(URHO3D_MATERIAL_HAS_EMISSIVE)
        #define GetAmbientLighting(vertexLighting, uv, albedo) \
            (texture2D(sEmissiveMap, (uv)).rgb * (vertexLighting) * (albedo))
    #elif defined(URHO3D_MATERIAL_HAS_EMISSIVE)
        #define GetAmbientLighting(vertexLighting, uv, albedo) \
            ((vertexLighting) * (albedo) + cMatEmissiveColor * texture2D(sEmissiveMap, (uv)).rgb)
    #else
        #define GetAmbientLighting(vertexLighting, uv, albedo) \
            ((vertexLighting) * (albedo) + cMatEmissiveColor)
    #endif
#endif // URHO3D_AMBIENT_PASS

#endif // URHO3D_PIXEL_SHADER
#endif // _MATERIAL_GLSL_
