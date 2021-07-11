#ifndef _INDIRECT_LIGHTING_GLSL_
#define _INDIRECT_LIGHTING_GLSL_

#ifndef _GAMMA_CORRECTION_GLSL_
    #error Include _GammaCorrection.glsl before _IndirectLighting.glsl
#endif

#ifdef URHO3D_VERTEX_SHADER

// Evalualte linear part of spherical harmonics
half3 EvaluateSH01(const half4 normal, const half4 SHAr, const half4 SHAg, const half4 SHAb)
{
    half3 value;
    value.r = dot(normal, SHAr);
    value.g = dot(normal, SHAg);
    value.b = dot(normal, SHAb);
    return value;
}

// Evalualte quadratic part of spherical harmonics
half3 EvaluateSH2(const half4 normal, const half4 SHBr, const half4 SHBg, const half4 SHBb, const half4 SHC)
{
    half4 b = normal.xyzz * normal.yzzx;
    half c = normal.x * normal.x - normal.y * normal.y;

    half3 value;
    value.r = dot(b, SHBr);
    value.g = dot(b, SHBg);
    value.b = dot(b, SHBb);
    value += c * SHC.rgb;
    return value;
}

#ifdef URHO3D_NUM_VERTEX_LIGHTS
    // Calculate intensity of vertex light
    float GetVertexLight(const int index, const vec3 worldPos, const half3 normal)
    {
        half3 lightDir = cVertexLights[index * 3 + 1].xyz;
        vec3 lightPos = cVertexLights[index * 3 + 2].xyz;
        half invRange = cVertexLights[index * 3].w;
        half cutoff = cVertexLights[index * 3 + 1].w;
        half invCutoff = cVertexLights[index * 3 + 2].w;

        // Directional light
        if (invRange == 0.0)
        {
            return VERTEX_ADJUST_NoL(dot(normal, lightDir));
        }
        // Point/spot light
        else
        {
            half3 lightVec = (lightPos - worldPos) * invRange;
            half lightDist = length(lightVec);
            half3 localDir = lightVec / lightDist;
            half atten = max(0.0, 1.0 - lightDist);
            half spotEffect = dot(localDir, lightDir);
            half spotAtten = clamp((spotEffect - cutoff) * invCutoff, 0.0, 1.0);
            return VERTEX_ADJUST_NoL(dot(normal, localDir)) * atten * atten * spotAtten;
        }
    }
#endif

#ifdef URHO3D_AMBIENT_PASS
    // Calculate ambient lighting from zones and/or spherical harmonics
    // cAmbient and cAmbientColor should contain color in light space.
    // cSH* should contain harmonics in linear space.
    #if defined(URHO3D_AMBIENT_DIRECTIONAL)
        #ifdef URHO3D_SURFACE_VOLUMETRIC
            #define GetAmbientLight(normal) LinearToLightSpace(vec3(cSHAr.w, cSHAg.w, cSHAb.w))
        #else
            #define GetAmbientLight(normal) LinearToLightSpace(EvaluateSH01(normal, cSHAr, cSHAg, cSHAb) + EvaluateSH2(normal, cSHBr, cSHBg, cSHBb, cSHC))
        #endif
    #elif defined(URHO3D_AMBIENT_FLAT)
        #define GetAmbientLight(normal) cAmbient.rgb
    #elif defined(URHO3D_AMBIENT_CONSTANT)
        #define GetAmbientLight(normal) cAmbientColor.rgb
    #endif

    // Calculate combined ambient lighting from zones, spherical harmonics and vertex lights
    // cVertexLights should contain light color in light space.
    half3 GetAmbientAndVertexLights(const vec3 position, const half3 normal)
    {
        half3 result = GetAmbientLight(vec4(normal, 1.0));

        #ifdef URHO3D_NUM_VERTEX_LIGHTS
            for (int i = 0; i < URHO3D_NUM_VERTEX_LIGHTS; ++i)
                result += GetVertexLight(i, position, normal) * cVertexLights[i * 3].rgb;
        #endif

        return result;
    }
#endif // URHO3D_AMBIENT_PASS

#endif // URHO3D_VERTEX_SHADER
#endif // _INDIRECT_LIGHTING_GLSL_
