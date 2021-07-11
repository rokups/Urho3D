#include "_Material.glsl"

#ifdef URHO3D_VERTEX_SHADER
void main()
{
    VertexTransform vertexTransform = GetVertexTransform();
    FillVertexOutputs(vertexTransform);
}
#endif

#ifdef URHO3D_PIXEL_SHADER
void main()
{
#ifdef URHO3D_DEPTH_ONLY_PASS
    DefaultPixelShader();
#else
    SurfaceData surfaceData;

    FillSurfaceCommon(surfaceData);
    FillSurfaceNormal(surfaceData);
    FillSurfaceMetallicRoughnessOcclusion(surfaceData);
    FillSurfaceReflectionColor(surfaceData);
    FillSurfaceBackground(surfaceData);
    FillSurfaceAlbedoSpecular(surfaceData);
    FillSurfaceEmission(surfaceData);

    half3 finalColor = GetFinalColor(surfaceData);
    gl_FragColor.rgb = ApplyFog(finalColor, surfaceData.fogFactor);
    gl_FragColor.a = GetFinalAlpha(surfaceData);
#endif
}
#endif
