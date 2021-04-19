#define URHO3D_CUSTOM_MATERIAL_UNIFORMS
#define URHO3D_IGNORE_MATERIAL_DIFFUSE
#define URHO3D_IGNORE_MATERIAL_NORMAL
#define URHO3D_IGNORE_MATERIAL_SPECULAR
#define URHO3D_IGNORE_MATERIAL_EMISSIVE

#include "_Config.glsl"
#include "_Uniforms.glsl"

UNIFORM_BUFFER_BEGIN(4, Material)
    DEFAULT_MATERIAL_UNIFORMS
    UNIFORM(half2 cDetailTiling)
UNIFORM_BUFFER_END(4, Material)

#include "_Material.glsl"

VERTEX_OUTPUT_HIGHP(vec2 vDetailTexCoord)

#ifdef URHO3D_VERTEX_SHADER
void main()
{
    VertexTransform vertexTransform = GetVertexTransform();
    FillVertexOutputs(vertexTransform);
    vDetailTexCoord = vTexCoord * cDetailTiling;
}
#endif

#ifdef URHO3D_PIXEL_SHADER
void main()
{
    SurfaceData surfaceData;

    FillSurfaceCommon(surfaceData);
    FillSurfaceNormal(surfaceData);
    FillSurfaceMetallicRoughnessOcclusion(surfaceData);
    FillSurfaceExternal(surfaceData);

    half3 weights = texture2D(sDiffMap, vTexCoord).rgb;
    half sumWeights = weights.r + weights.g + weights.b;
    weights /= sumWeights;
    surfaceData.albedo = cMatDiffColor * (
        weights.r * texture2D(sNormalMap, vDetailTexCoord) +
        weights.g * texture2D(sSpecMap, vDetailTexCoord) +
        weights.b * texture2D(sEmissiveMap, vDetailTexCoord)
    );

    surfaceData.specular = cMatSpecColor.rgb;
#ifdef URHO3D_SURFACE_NEED_AMBIENT
    surfaceData.emission = cMatEmissiveColor;
#endif

    half3 finalColor = GetFinalColor(surfaceData);
    gl_FragColor.rgb = ApplyFog(finalColor, surfaceData.fogFactor);
    gl_FragColor.a = GetFinalAlpha(surfaceData);
}
#endif
