#define URHO3D_PIXEL_NEED_NORMAL
#include "_Material.glsl"

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

#ifdef URHO3D_GBUFFER_PASS
    float roughness = 1.0 - cMatSpecColor.a / 255.0;
    vec3 finalColor = GetSurfaceAmbient(surfaceData);

    gl_FragData[0] = vec4(ApplyFog(finalColor, surfaceData.fogFactor), 1.0);
    gl_FragData[1] = vec4(surfaceData.fogFactor * surfaceData.albedo.rgb, 0.0);
    gl_FragData[2] = vec4(surfaceData.fogFactor * surfaceData.specular, roughness);
    gl_FragData[3] = vec4(surfaceData.normal * 0.5 + 0.5, 0.0);
#else
    #ifdef URHO3D_AMBIENT_PASS
        vec3 finalColor = GetSurfaceAmbient(surfaceData);
    #else
        vec3 finalColor = vec3(0.0);
    #endif

    #ifdef URHO3D_HAS_PIXEL_LIGHT
        PixelLightData pixelLightData = GetForwardPixelLightData();
        #ifdef URHO3D_LIGHT_HAS_SPECULAR
            finalColor += GetBlinnPhongDiffuseSpecular(pixelLightData, surfaceData.normal, surfaceData.albedo.rgb,
                surfaceData.specular, surfaceData.eyeVec, cMatSpecColor.a);
        #else
            finalColor += GetBlinnPhongDiffuse(pixelLightData, surfaceData.normal, surfaceData.albedo.rgb);
        #endif
    #endif

    gl_FragColor = vec4(ApplyFog(finalColor, surfaceData.fogFactor), surfaceData.albedo.a);
#endif
}
#endif
