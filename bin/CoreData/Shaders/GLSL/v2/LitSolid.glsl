#include "_Config.glsl"
#include "_Uniforms.glsl"
#include "_VertexLayout.glsl"
#include "_VertexTransform.glsl"
#include "_PixelOutput.glsl"
#include "_GammaCorrection.glsl"
#include "_AmbientLighting.glsl"
#include "_Samplers.glsl"
#include "_Shadow.glsl"
#include "Lighting.glsl"
#include "Fog.glsl"

VERTEX_OUTPUT(vec2 vTexCoord)
#ifdef URHO3D_HAS_LIGHTMAP
    VERTEX_OUTPUT(vec2 vTexCoord2)
#endif
#ifdef URHO3D_VERTEX_HAS_COLOR
    VERTEX_OUTPUT(vec4 vColor)
#endif
#ifdef URHO3D_NORMAL_MAPPING
    VERTEX_OUTPUT(vec4 vTangent)
    VERTEX_OUTPUT(vec2 vBitangentXY)
#endif

VERTEX_OUTPUT(vec3 vNormal)
VERTEX_OUTPUT(vec4 vWorldPos)

#ifdef URHO3D_HAS_AMBIENT_OR_VERTEX_LIGHT
    VERTEX_OUTPUT(vec3 vAmbientAndVertexLigthing)
#endif
#ifdef URHO3D_HAS_SHADOW
    VERTEX_OUTPUT(optional_highp vec4 vShadowPos[URHO3D_SHADOW_NUM_CASCADES])
#endif

#ifdef PERPIXEL
    #ifdef SPOTLIGHT
        VERTEX_OUTPUT(vec4 vSpotPos)
    #endif
    #ifdef POINTLIGHT
        VERTEX_OUTPUT(vec3 vCubeMaskVec)
    #endif
#else
    VERTEX_OUTPUT(vec4 vScreenPos)
    #ifdef ENVCUBEMAP
        VERTEX_OUTPUT(vec3 vReflectionVec)
    #endif
#endif

#ifdef URHO3D_VERTEX_SHADER
void main()
{
    VertexTransform vertexTransform = GetVertexTransform();
    gl_Position = WorldToClipSpace(vertexTransform.position.xyz);

    vTexCoord = GetTransformedTexCoord();
    vNormal = vertexTransform.normal;
    vWorldPos = vec4(vertexTransform.position.xyz, GetDepth(gl_Position));

    #ifdef URHO3D_VERTEX_HAS_COLOR
        vColor = iColor;
    #endif

    #ifdef URHO3D_NORMAL_MAPPING
        vTangent = vec4(vertexTransform.tangent.xyz, vertexTransform.bitangent.z);
        vBitangentXY = vertexTransform.bitangent.xy;
    #endif

    #ifdef URHO3D_HAS_LIGHTMAP
        vTexCoord2 = GetLightMapTexCoord();
    #endif

    #ifdef URHO3D_HAS_AMBIENT_OR_VERTEX_LIGHT
        vAmbientAndVertexLigthing = GetAmbientAndVertexLights(vertexTransform.position.xyz, vertexTransform.normal);
    #endif

    #ifdef URHO3D_HAS_SHADOW
        WorldSpaceToShadowCoord(vShadowPos, vertexTransform.position);
    #endif

    #ifdef SPOTLIGHT
        vSpotPos = vertexTransform.position * cLightMatrices[0];
    #endif

    #ifdef POINTLIGHT
        vCubeMaskVec = (vertexTransform.position.xyz - cLightPos.xyz) * mat3(cLightMatrices[0][0].xyz, cLightMatrices[0][1].xyz, cLightMatrices[0][2].xyz);
    #endif
}
#endif

#ifdef URHO3D_PIXEL_SHADER
void main()
{
    // Get material diffuse albedo
    #ifdef DIFFMAP
        vec4 diffInput = texture2D(sDiffMap, vTexCoord.xy);
        #ifdef ALPHAMASK
            if (diffInput.a < 0.5)
                discard;
        #endif
        vec4 diffColor = DiffMap_ToLight(cMatDiffColor * diffInput);
    #else
        vec4 diffColor = GammaToLightSpaceAlpha(cMatDiffColor);
    #endif

    #ifdef URHO3D_VERTEX_HAS_COLOR
        diffColor *= vColor;
    #endif

    // Get material specular albedo
    #ifdef SPECMAP
        vec3 specColor = cMatSpecColor.rgb * texture2D(sSpecMap, vTexCoord.xy).rgb;
    #else
        vec3 specColor = cMatSpecColor.rgb;
    #endif

    // Get normal
    #ifdef URHO3D_NORMAL_MAPPING
        mat3 tbn = mat3(vTangent.xyz, vec3(vBitangentXY.xy, vTangent.w), vNormal);
        vec3 normal = normalize(tbn * DecodeNormal(texture2D(sNormalMap, vTexCoord.xy)));
    #else
        vec3 normal = normalize(vNormal);
    #endif

    // Get fog factor
    #ifdef HEIGHTFOG
        float fogFactor = GetHeightFogFactor(vWorldPos.w, vWorldPos.y);
    #else
        float fogFactor = GetFogFactor(vWorldPos.w);
    #endif

    #if defined(PASS_BASE) || defined(PASS_ALPHA)
        // Per-pixel forward lighting
        vec3 lightColor;
        vec3 lightDir;
        vec3 finalColor;

        float diff = GetDiffuse(normal, vWorldPos.xyz, lightDir);

        #ifdef SHADOW
            diff *= GetForwardShadow(vShadowPos, vWorldPos.w);
        #endif

        #if defined(SPOTLIGHT)
            lightColor = vSpotPos.w > 0.0 ? texture2DProj(sLightSpotMap, vSpotPos).rgb * cLightColor.rgb : vec3(0.0, 0.0, 0.0);
        #elif defined(CUBEMASK)
            lightColor = textureCube(sLightCubeMap, vCubeMaskVec).rgb * cLightColor.rgb;
        #else
            lightColor = cLightColor.rgb;
        #endif

        #ifdef SPECULAR
            float spec = GetSpecular(normal, cCameraPosPS - vWorldPos.xyz, lightDir, cMatSpecColor.a);
            finalColor = diff * lightColor * (diffColor.rgb + spec * specColor * cLightColor.a);
        #else
            finalColor = diff * lightColor * diffColor.rgb;
        #endif

        #ifdef URHO3D_HAS_AMBIENT_OR_VERTEX_LIGHT
            finalColor += vAmbientAndVertexLigthing * diffColor.rgb;
        #endif

        #ifdef AMBIENT
            #ifdef LIGHTMAP
                finalColor += texture2D(sEmissiveMap, vTexCoord2).rgb * 2.0 * diffColor.rgb;
            #elif defined(EMISSIVEMAP)
                finalColor += cMatEmissiveColor * texture2D(sEmissiveMap, vTexCoord.xy).rgb;
            #else
                finalColor += cMatEmissiveColor;
            #endif
            gl_FragColor = vec4(GetFog(finalColor, fogFactor), diffColor.a);
        #else
            gl_FragColor = vec4(GetLitFog(finalColor, fogFactor), diffColor.a);
        #endif
    #elif defined(PASS_DEFERRED)
        // Fill deferred G-buffer
        float specIntensity = specColor.g;
        float specPower = cMatSpecColor.a / 255.0;

        vec3 finalColor = vAmbientAndVertexLigthing * diffColor.rgb;
        /*#ifdef AO
            // If using AO, the vertex light ambient is black, calculate occluded ambient here
            finalColor += texture2D(sEmissiveMap, vTexCoord2).rgb * cAmbientColor.rgb * diffColor.rgb;
        #endif*/

        #ifdef ENVCUBEMAP
            finalColor += cMatEnvMapColor * textureCube(sEnvCubeMap, reflect(vReflectionVec, normal)).rgb;
        #endif
        #ifdef LIGHTMAP
            finalColor += texture2D(sEmissiveMap, vTexCoord2).rgb * 2.0 * diffColor.rgb;
        #elif defined(EMISSIVEMAP)
            finalColor += cMatEmissiveColor * texture2D(sEmissiveMap, vTexCoord.xy).rgb;
        #else
            finalColor += cMatEmissiveColor;
        #endif

        gl_FragData[0] = vec4(GetFog(finalColor, fogFactor), 1.0);
        gl_FragData[1] = fogFactor * vec4(diffColor.rgb, specIntensity);
        gl_FragData[2] = vec4(normal * 0.5 + 0.5, specPower);
    #endif
}
#endif
