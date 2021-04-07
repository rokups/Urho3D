#define URHO3D_PIXEL_NEED_EYE_VECTOR
#define URHO3D_PIXEL_NEED_BACKGROUND_COLOR
#define URHO3D_PIXEL_NEED_NORMAL
#define URHO3D_PIXEL_NEED_NORMAL_IN_TANGENT_SPACE

#ifndef NORMALMAP
    #define NORMALMAP
#endif

#ifndef PBR
    #define PBR
#endif

#include "_Material.glsl"

/*VERTEX_OUTPUT(vec4 vScreenPos)
VERTEX_OUTPUT(vec2 vReflectUV)
VERTEX_OUTPUT(vec2 vWaterUV)
VERTEX_OUTPUT(vec4 vEyeVec)
VERTEX_OUTPUT(vec3 vNormal)*/

// TODO(renderer): Move to constants
const vec2 cNoiseSpeed = vec2(0.05, 0.05);
const float cNoiseTiling = 2.0;
const float cNoiseStrength = 0.02;
const float cFresnelPower = 8.0;
const vec3 cWaterTint = vec3(0.8, 0.8, 1.0);

#ifdef URHO3D_VERTEX_SHADER
void main()
{
    VertexTransform vertexTransform = GetVertexTransform();
    vec2 uv = GetTransformedTexCoord() * cNoiseTiling + cElapsedTime * cNoiseSpeed;
    FillCommonVertexOutput(vertexTransform, uv);

    /*gl_Position = WorldToClipSpace(vertexTransform.position.xyz);
    vScreenPos = GetScreenPos(gl_Position);
    // GetQuadTexCoord() returns a vec2 that is OK for quad rendering; multiply it with output W
    // coordinate to make it work with arbitrary meshes such as the water plane (perform divide in pixel shader)
    // Also because the quadTexCoord is based on the clip position, and Y is flipped when rendering to a texture
    // on OpenGL, must flip again to cancel it out
    vReflectUV = GetQuadTexCoord(gl_Position);
    vReflectUV.y = 1.0 - vReflectUV.y;
    vReflectUV *= gl_Position.w;
    vWaterUV = iTexCoord * cNoiseTiling + cElapsedTime * cNoiseSpeed;
    vNormal = vertexTransform.normal;
    vEyeVec = vec4(cCameraPos - vertexTransform.position.xyz, GetDepth(gl_Position));*/
}
#endif

#ifdef URHO3D_PIXEL_SHADER
void main()
{
    FragmentData fragmentData = GetFragmentData();
    SurfaceGeometryData surfaceGeometryData = GetSurfaceGeometryData();

    fragmentData.screenUV += surfaceGeometryData.normalInTangentSpace.xy * cNoiseStrength;

    SetupFragmentReflectionColor(fragmentData, surfaceGeometryData);
    SetupFragmentBackgroundColor(fragmentData);

    SurfaceMaterialData surfaceMaterialData;
    surfaceMaterialData.albedo = vec4(0.0, 0.0, 0.0, 1.0);
#ifdef URHO3D_IS_LIT
    surfaceMaterialData.specular = cMatSpecColor.rgb;
    surfaceMaterialData.emission = cMatEmissiveColor;
#endif

    half3 finalColor = GetFinalColor(fragmentData, surfaceGeometryData, surfaceMaterialData);
    half3 outputColor = ApplyFog(finalColor, fragmentData.fogFactor);
#ifdef URHO3D_ADDITIVE_LIGHT_PASS
    gl_FragColor = vec4(outputColor, surfaceMaterialData.albedo.a);
#else
    gl_FragColor.rgb = outputColor + fragmentData.backgroundColor * surfaceMaterialData.albedo.a;
    gl_FragColor.a = 1.0;
#endif

}
#endif
