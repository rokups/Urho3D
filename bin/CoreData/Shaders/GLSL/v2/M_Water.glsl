#define URHO3D_PIXEL_NEED_EYE_VECTOR
#define URHO3D_PIXEL_NEED_BACKGROUND_COLOR
#define URHO3D_PIXEL_NEED_NORMAL
#define URHO3D_PIXEL_NEED_NORMAL_IN_TANGENT_SPACE
#define URHO3D_CUSTOM_MATERIAL_UNIFORMS

#ifndef NORMALMAP
    #define NORMALMAP
#endif

#ifndef ENVCUBEMAP
    #define ENVCUBEMAP
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

    // Apply noise to screen position used for background sampling
    fragmentData.screenPos += surfaceGeometryData.normalInTangentSpace.xy * cNoiseStrength;

    SetupFragmentReflectionColor(fragmentData, surfaceGeometryData);
    SetupFragmentBackgroundColor(fragmentData);

    // Water doesn't accept diffuse lighting, set albedo to zero
    SurfaceMaterialData surfaceMaterialData;
    surfaceMaterialData.albedo = vec4(0.0);
    surfaceMaterialData.specular = cMatSpecColor.rgb * (1.0 - surfaceGeometryData.oneMinusReflectivity);
    surfaceMaterialData.emission = cMatEmissiveColor;

#ifdef URHO3D_AMBIENT_PASS
    half NoV = clamp(dot(surfaceGeometryData.normal, fragmentData.eyeVec), 0.0, 1.0);
    #ifdef URHO3D_PHYSICAL_MATERIAL
        half4 reflectedColor = Indirect_PBRWater(fragmentData, surfaceMaterialData.specular, NoV);
    #else
        half4 reflectedColor = Indirect_SimpleWater(fragmentData, cMatEnvMapColor, NoV);
    #endif
#else
    half4 reflectedColor = vec4(0.0);
#endif

#ifdef URHO3D_HAS_PIXEL_LIGHT
    reflectedColor.rgb += CalculateDirectLighting(fragmentData, surfaceGeometryData, surfaceMaterialData);
#endif

#ifdef URHO3D_ADDITIVE_LIGHT_PASS
    gl_FragColor = vec4(ApplyFog(reflectedColor.rgb, fragmentData.fogFactor), 0.0);
#else
    gl_FragColor = vec4(mix(fragmentData.backgroundColor, reflectedColor.rgb, reflectedColor.a), 1.0);
#endif

}
#endif
