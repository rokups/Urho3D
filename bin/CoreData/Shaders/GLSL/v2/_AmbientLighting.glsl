#ifndef _AMBIENT_LIGHTING_GLSL_
#define _AMBIENT_LIGHTING_GLSL_

#ifndef _GAMMA_CORRECTION_GLSL_
    #error Include "_GammaCorrection.glsl" before "_AmbientLighting.glsl"
#endif

#ifdef URHO3D_VERTEX_SHADER

// Evalualte linear part of spherical harmonics
vec3 EvaluateSH01(vec4 normal, vec4 SHAr, vec4 SHAg, vec4 SHAb)
{
    vec3 value;
    value.r = dot(normal, SHAr);
    value.g = dot(normal, SHAg);
    value.b = dot(normal, SHAb);
    return value;
}

// Evalualte quadratic part of spherical harmonics
vec3 EvaluateSH2(vec4 normal, vec4 SHBr, vec4 SHBg, vec4 SHBb, vec4 SHC)
{
    vec4 b = normal.xyzz * normal.yzzx;
    float c = normal.x * normal.x - normal.y * normal.y;

    vec3 value;
    value.r = dot(b, SHBr);
    value.g = dot(b, SHBg);
    value.b = dot(b, SHBb);
    value += c * SHC.rgb;
    return value;
}

#ifdef URHO3D_NUM_VERTEX_LIGHTS
    // Calculate intensity of vertex light
    float GetVertexLight(int index, vec3 worldPos, vec3 normal)
    {
        vec3 lightDir = cVertexLights[index * 3 + 1].xyz;
        vec3 lightPos = cVertexLights[index * 3 + 2].xyz;
        float invRange = cVertexLights[index * 3].w;
        float cutoff = cVertexLights[index * 3 + 1].w;
        float invCutoff = cVertexLights[index * 3 + 2].w;

        // Directional light
        if (invRange == 0.0)
        {
            return CONVERT_N_DOT_L(dot(normal, lightDir));
        }
        // Point/spot light
        else
        {
            vec3 lightVec = (lightPos - worldPos) * invRange;
            float lightDist = length(lightVec);
            vec3 localDir = lightVec / lightDist;
            float atten = max(0.0, 1.0 - lightDist);
            float spotEffect = dot(localDir, lightDir);
            float spotAtten = clamp((spotEffect - cutoff) * invCutoff, 0.0, 1.0);
            return CONVERT_N_DOT_L(dot(normal, localDir)) * atten * atten * spotAtten;
        }
    }
#endif

#ifdef URHO3D_AMBIENT_PASS
    // Calculate ambient lighting from zones and/or spherical harmonics
    // cAmbient and cAmbientColor should contain color in light space.
    // cSH* should contain harmonics in linear space.
    #if defined(URHO3D_AMBIENT_DIRECTIONAL)
        #define GetAmbientLight(normal) LinearToLightSpace(EvaluateSH01(normal, cSHAr, cSHAg, cSHAb) + EvaluateSH2(normal, cSHBr, cSHBg, cSHBb, cSHC))
    #elif defined(URHO3D_AMBIENT_FLAT)
        #define GetAmbientLight(normal) cAmbient.rgb
    #elif defined(URHO3D_AMBIENT_CONSTANT)
        #define GetAmbientLight(normal) cAmbientColor.rgb
    #endif

    // Calculate combined ambient lighting from zones, spherical harmonics and vertex lights
    // cVertexLights should contain light color in light space.
    vec3 GetAmbientAndVertexLights(vec3 position, vec3 normal)
    {
        vec3 result = GetAmbientLight(vec4(normal, 1.0));

        #ifdef URHO3D_NUM_VERTEX_LIGHTS
            for (int i = 0; i < URHO3D_NUM_VERTEX_LIGHTS; ++i)
                result += GetVertexLight(i, position, normal) * cVertexLights[i * 3].rgb;
        #endif

        return result;
    }
#endif // URHO3D_AMBIENT_PASS

#endif // URHO3D_VERTEX_SHADER
#endif // _AMBIENT_LIGHTING_GLSL_
