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

VERTEX_OUTPUT(vec2 vDetailTexCoord)

#ifdef URHO3D_VERTEX_SHADER
void main()
{
    VertexTransform vertexTransform = GetVertexTransform();
    vec2 texCoord = GetTransformedTexCoord();
    vDetailTexCoord = texCoord * cDetailTiling;
    FillCommonVertexOutput(vertexTransform, texCoord);
}
#endif

#ifdef URHO3D_PIXEL_SHADER
void main()
{
    FragmentData fragmentData = GetFragmentData();
    SurfaceGeometryData surfaceGeometryData = GetSurfaceGeometryData();

    SetupFragmentReflectionColor(fragmentData, surfaceGeometryData);
    SetupFragmentBackgroundDepth(fragmentData);

    SurfaceMaterialData surfaceMaterialData;

    half3 weights = texture2D(sDiffMap, vTexCoord).rgb;
    half sumWeights = weights.r + weights.g + weights.b;
    weights /= sumWeights;
    surfaceMaterialData.albedo = cMatDiffColor * (
        weights.r * texture2D(sNormalMap, vDetailTexCoord) +
        weights.g * texture2D(sSpecMap, vDetailTexCoord) +
        weights.b * texture2D(sEmissiveMap, vDetailTexCoord)
    );

#ifdef URHO3D_IS_LIT
    surfaceMaterialData.specular = cMatSpecColor.rgb;
    surfaceMaterialData.emission = cMatEmissiveColor;
#endif

    half3 finalColor = GetFinalColor(fragmentData, surfaceGeometryData, surfaceMaterialData);
    gl_FragColor.rgb = ApplyFog(finalColor, fragmentData.fogFactor);
    gl_FragColor.a = GetFinalAlpha(fragmentData, surfaceMaterialData);
}
#endif
