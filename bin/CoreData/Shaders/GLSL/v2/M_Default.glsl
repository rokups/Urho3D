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
    SurfaceData surfaceData;

    FillFragmentFogFactor(surfaceData);
    FillFragmentAmbient(surfaceData);
    FillFragmentEyeVector(surfaceData);
    FillFragmentScreenPosition(surfaceData);
    FillFragmentNormal(surfaceData);
    FillFragmentMetallicRoughnessOcclusion(surfaceData);

    AdjustFragmentRoughness(surfaceData);
    FillFragmentBackgroundDepth(surfaceData);
    FillFragmentReflectionColor(surfaceData);

    FillFragmentAlbedoSpecular(surfaceData);
    FillFragmentEmission(surfaceData);

    half3 finalColor = GetFinalColor(surfaceData);
    gl_FragColor.rgb = ApplyFog(finalColor, surfaceData.fogFactor);
    gl_FragColor.a = GetFinalAlpha(surfaceData);
}
#endif
