#define MRT_COUNT 6
#include "Uniforms.glsl"
#include "Samplers.glsl"
#include "Transform.glsl"
#include "ScreenPos.glsl"

#if !defined(GL3) || !defined(USE_CBUFFERS)

uniform float cLightmapLayer;
uniform float cLightmapGeometry;
uniform vec2 cLightmapPositionBias;

#else

uniform LightmapVS
{
    float cLightmapLayer;
    float cLightmapGeometry;
    vec2 cLightmapPositionBias;
}

#endif

varying vec2 vTexCoord;
varying vec3 vNormal;
varying vec4 vWorldPos;
varying vec4 vMetadata;

void VS()
{
    mat4 modelMatrix = iModelMatrix;
    vec3 worldPos = GetWorldPos(modelMatrix);
    vec2 lightmapUV = iTexCoord1 * cLMOffset.xy + cLMOffset.zw;

    gl_Position = vec4(lightmapUV * vec2(2, 2) + vec2(-1, -1), cLightmapLayer, 1);
    vNormal = GetWorldNormal(modelMatrix);
    vWorldPos = vec4(worldPos, 1.0);
    vMetadata = vec4(cLightmapGeometry, cLightmapPositionBias.x, cLightmapPositionBias.y, 0.0);
    vTexCoord = GetTexCoord(iTexCoord);
}

void PS()
{
    vec4 diffColor = cMatDiffColor;
    #ifdef DIFFMAP
        diffColor *= texture2D(sDiffMap, vTexCoord.xy);
    #endif

    vec3 emissiveColor = cMatEmissiveColor;
    #ifdef EMISSIVEMAP
        emissiveColor *= texture2D(sEmissiveMap, vTexCoord.xy).rgb;
    #endif

    vec3 normal = normalize(vNormal);

    vec3 dPdx = dFdx(vWorldPos.xyz);
    vec3 dPdy = dFdy(vWorldPos.xyz);
    vec3 faceNormal = normalize(cross(dPdx, dPdy));
    if (dot(faceNormal, normal) < 0)
        faceNormal *= -1.0;

    vec3 dPmax = max(abs(dPdx), abs(dPdy));
    float texelRadius = max(dPmax.x, max(dPmax.y, dPmax.z)) * 1.4142135 * 0.5;

    float scaledBias = vMetadata.y;
    float constBias = vMetadata.z;
    vec3 biasScale = max(abs(vWorldPos.xyz), vec3(1.0, 1.0, 1.0));
    vec3 position = vWorldPos.xyz + sign(faceNormal) * biasScale * scaledBias + faceNormal * constBias;

    gl_FragData[0] = vec4(position, vMetadata.x);
    gl_FragData[1] = vec4(position, texelRadius);
    gl_FragData[2] = vec4(faceNormal, 1.0);
    gl_FragData[3] = vec4(normal, 1.0);
    gl_FragData[4] = vec4(diffColor.rgb * diffColor.a, 1.0);
    gl_FragData[5] = vec4(emissiveColor.rgb, 1.0);
}
