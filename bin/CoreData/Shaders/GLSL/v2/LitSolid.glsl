#include "_Material.glsl"
#include "_BRDF.glsl"

#ifdef URHO3D_VERTEX_SHADER
void main()
{
    VertexTransform vertexTransform = GetVertexTransform();
    FillCommonVertexOutput(vertexTransform, GetTransformedTexCoord());
}
#endif

#ifdef URHO3D_PIXEL_SHADER
void main()
{
    SurfaceData surfaceData = GetCommonSurfaceData();

#ifdef URHO3D_AMBIENT_PASS
    #ifdef URHO3D_PHYSICAL_MATERIAL
        half NoV = abs(dot(surfaceData.normal, surfaceData.eyeVec)) + 1e-5;
        vec3 finalColor = Indirect_PBR(surfaceData, NoV);
    #else
        vec3 finalColor = Indirect_Simple(surfaceData);
    #endif
#else
    vec3 finalColor = vec3(0.0);
#endif

#ifdef URHO3D_GBUFFER_PASS
    #ifdef URHO3D_PHYSICAL_MATERIAL
        float roughness = surfaceData.roughness;
    #else
        float roughness = 1.0 - cMatSpecColor.a / 255.0;
    #endif

    gl_FragData[0] = vec4(ApplyFog(finalColor, surfaceData.fogFactor), 1.0);
    gl_FragData[1] = vec4(surfaceData.fogFactor * surfaceData.albedo.rgb, 0.0);
    gl_FragData[2] = vec4(surfaceData.fogFactor * surfaceData.specular, roughness);
    gl_FragData[3] = vec4(surfaceData.normal * 0.5 + 0.5, 0.0);
#else
    #ifdef URHO3D_HAS_PIXEL_LIGHT
        DirectLightData lightData = GetForwardDirectLightData();

        #if defined(URHO3D_PHYSICAL_MATERIAL) || defined(URHO3D_LIGHT_HAS_SPECULAR)
            half3 halfVec = normalize(surfaceData.eyeVec + lightData.lightVec.xyz);
        #endif

        #ifdef URHO3D_PHYSICAL_MATERIAL
            half3 lightColor = Direct_PBR(lightData.lightColor, surfaceData.albedo.rgb, surfaceData.specular, surfaceData.roughness,
                lightData.lightVec.xyz, surfaceData.normal, surfaceData.eyeVec, halfVec);
        #else
            #ifdef URHO3D_LIGHT_HAS_SPECULAR
                half3 lightColor = Direct_SimpleSpecular(lightData.lightColor,
                    surfaceData.albedo.rgb, surfaceData.specular,
                    lightData.lightVec.xyz, surfaceData.normal, halfVec, cMatSpecColor.a);
            #else
                half3 lightColor = Direct_Simple(lightData.lightColor,
                    surfaceData.albedo.rgb, lightData.lightVec.xyz, surfaceData.normal);
            #endif
        #endif
        finalColor += lightColor * GetDirectLightAttenuation(lightData);
    #endif

    gl_FragColor = vec4(ApplyFog(finalColor, surfaceData.fogFactor), surfaceData.albedo.a);
#endif
}
#endif
