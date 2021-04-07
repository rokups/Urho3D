#define URHO3D_PIXEL_NEED_EYE_VECTOR
#define URHO3D_PIXEL_NEED_BACKGROUND_COLOR
#define URHO3D_PIXEL_NEED_NORMAL
#define URHO3D_PIXEL_NEED_NORMAL_IN_TANGENT_SPACE
#define URHO3D_CUSTOM_MATERIAL_UNIFORMS

#ifndef NORMALMAP
    #define NORMALMAP
#endif

#ifndef PBR
    #define PBR
#endif

#include "_Config.glsl"
#include "_Uniforms.glsl"

UNIFORM_BUFFER_BEGIN(4, Material)
    DEFAULT_MATERIAL_UNIFORMS
    UNIFORM(half2 cNoiseSpeed)
    UNIFORM(half cNoiseStrength)
UNIFORM_BUFFER_END(4, Material)

#include "_Material.glsl"

#ifdef URHO3D_VERTEX_SHADER
void main()
{
    VertexTransform vertexTransform = GetVertexTransform();
    vec2 uv = GetTransformedTexCoord() + cElapsedTime * cNoiseSpeed;
    FillCommonVertexOutput(vertexTransform, uv);
}
#endif

#ifdef URHO3D_PIXEL_SHADER
void main()
{
    FragmentData fragmentData = GetFragmentData();
    SurfaceGeometryData surfaceGeometryData = GetSurfaceGeometryData();

    fragmentData.screenPos += surfaceGeometryData.normalInTangentSpace.xy * cNoiseStrength;

    SetupFragmentReflectionColor(fragmentData, surfaceGeometryData);
    SetupFragmentBackgroundColor(fragmentData);

    SurfaceMaterialData surfaceMaterialData;
    surfaceMaterialData.albedo = vec4(0.0, 0.0, 0.0, 0.0);
#ifdef URHO3D_IS_LIT
    surfaceMaterialData.specular = cMatSpecColor.rgb;
    surfaceMaterialData.emission = cMatEmissiveColor;
#endif

    half3 finalColor = GetFinalColor(fragmentData, surfaceGeometryData, surfaceMaterialData);
    half3 outputColor = ApplyFog(finalColor, fragmentData.fogFactor);
#ifdef URHO3D_ADDITIVE_LIGHT_PASS
    gl_FragColor = vec4(outputColor, 0.0);
#else
    gl_FragColor.rgb = outputColor + fragmentData.backgroundColor;
    gl_FragColor.a = 1.0;
#endif

}
#endif
