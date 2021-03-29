#define UNLIT
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
    gl_FragData[0] = vec4(ApplyFog(surfaceData.albedo.rgb, surfaceData.fogFactor), 1.0);
    gl_FragData[1] = vec4(0.0, 0.0, 0.0, 0.0);
    gl_FragData[2] = vec4(0.0, 0.0, 0.0, 0.0);
    gl_FragData[3] = vec4(0.5, 0.5, 0.5, 0.0);
#else
    gl_FragColor = vec4(ApplyFog(surfaceData.albedo.rgb, surfaceData.fogFactor), surfaceData.albedo.a);
#endif
}
#endif
