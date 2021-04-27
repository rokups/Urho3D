#ifdef URHO3D_NUM_RENDER_TARGETS
    #undef URHO3D_NUM_RENDER_TARGETS
#endif
#define URHO3D_NUM_RENDER_TARGETS 6
#define URHO3D_PIXEL_NEED_NORMAL
#define URHO3D_CUSTOM_MATERIAL_UNIFORMS

#include "_Config.glsl"
#include "_GammaCorrection.glsl"

UNIFORM_BUFFER_BEGIN(4, Material)
    UNIFORM(float cLightmapLayer)
    UNIFORM(float cLightmapGeometry)
    UNIFORM(vec2 cLightmapPositionBias)

    UNIFORM(vec4 cUOffset)
    UNIFORM(vec4 cVOffset)
    UNIFORM(vec4 cLMOffset)

    UNIFORM(vec4 cMatDiffColor)
    UNIFORM(vec3 cMatEmissiveColor)
UNIFORM_BUFFER_END(4, Material)

#include "_Uniforms.glsl"
#include "_Samplers.glsl"
#include "_VertexLayout.glsl"

#include "_VertexTransform.glsl"

VERTEX_OUTPUT_HIGHP(vec2 vTexCoord)
VERTEX_OUTPUT(half3 vNormal)
VERTEX_OUTPUT_HIGHP(vec3 vWorldPos)
VERTEX_OUTPUT_HIGHP(vec4 vMetadata)

#ifdef URHO3D_VERTEX_SHADER
void main()
{
    VertexTransform vertexTransform = GetVertexTransform();
    vec2 lightmapUV = iTexCoord1 * cLMOffset.xy + cLMOffset.zw;

#ifdef URHO3D_FEATURE_FRAMEBUFFER_Y_INVERTED
    const vec4 scaleOffset = vec4(2, -2, -1, 1);
#else
    const vec4 scaleOffset = vec4(2, 2, -1, -1);
#endif

    gl_Position = vec4(lightmapUV * scaleOffset.xy + scaleOffset.zw, cLightmapLayer, 1);
    vNormal = vertexTransform.normal;
    vWorldPos = vertexTransform.position.xyz;
    vMetadata = vec4(cLightmapGeometry, cLightmapPositionBias.x, cLightmapPositionBias.y, 0.0);
    vTexCoord = GetTransformedTexCoord();
}
#endif

#ifdef URHO3D_PIXEL_SHADER
void main()
{
#ifdef URHO3D_MATERIAL_HAS_DIFFUSE
    vec4 albedoInput = texture2D(sDiffMap, vTexCoord);
    vec4 albedo = cMatDiffColor * DiffMap_ToLinear(albedoInput);
#else
    vec4 albedo = GammaToLinearSpaceAlpha(cMatDiffColor);
#endif

#ifdef URHO3D_MATERIAL_HAS_EMISSIVE
    vec4 emissiveInput = texture2D(sEmissiveMap, vTexCoord);
    vec3 emissive = EmissiveMap_ToLinear(cMatEmissiveColor.rgb * emissiveInput.rgb);
#else
    vec3 emissive = GammaToLinearSpace(cMatEmissiveColor);
#endif

    vec3 normal = normalize(vNormal);

    vec3 dPdx = dFdx(vWorldPos.xyz);
    vec3 dPdy = dFdy(vWorldPos.xyz);
    vec3 faceNormal = normalize(cross(dPdx, dPdy));
    if (dot(faceNormal, normal) < 0)
        faceNormal *= -1.0;

    vec3 dPmax = max(abs(dPdx), abs(dPdy));
    float texelRadius = max(dPmax.x, max(dPmax.y, dPmax.z)) * (1.4142135 * 0.5);

    float scaledBias = vMetadata.y;
    float constBias = vMetadata.z;
    vec3 biasScale = max(abs(vWorldPos.xyz), vec3(1.0, 1.0, 1.0));
    vec3 position = vWorldPos.xyz + sign(faceNormal) * biasScale * scaledBias + faceNormal * constBias;

    gl_FragData[0] = vec4(position, vMetadata.x);
    gl_FragData[1] = vec4(position, texelRadius);
    gl_FragData[2] = vec4(faceNormal, 1.0);
    gl_FragData[3] = vec4(normal, 1.0);
    gl_FragData[4] = vec4(albedo.rgb * albedo.a, 1.0);
    gl_FragData[5] = vec4(emissive, 1.0);
}
#endif
