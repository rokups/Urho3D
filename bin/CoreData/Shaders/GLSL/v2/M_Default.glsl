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
    FragmentData fragmentData = GetFragmentData();
    SurfaceGeometryData surfaceGeometryData = GetSurfaceGeometryData();

    SetupFragmentReflectionColor(fragmentData, surfaceGeometryData);
    SetupFragmentBackgroundDepth(fragmentData);

    SurfaceMaterialData surfaceMaterialData = GetSurfaceMaterialData(surfaceGeometryData.oneMinusReflectivity);

    half3 finalColor = GetFinalColor(fragmentData, surfaceGeometryData, surfaceMaterialData);
    gl_FragColor.rgb = ApplyFog(finalColor, fragmentData.fogFactor);
    gl_FragColor.a = GetFinalAlpha(fragmentData, surfaceMaterialData);
}
#endif
