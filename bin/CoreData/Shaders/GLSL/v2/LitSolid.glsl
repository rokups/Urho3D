#include "_Config.glsl"
#include "_Uniforms.glsl"
#include "_Samplers.glsl"
#include "_VertexLayout.glsl"
#include "_PixelOutput.glsl"

#include "_VertexTransform.glsl"
#include "_GammaCorrection.glsl"
#include "_AmbientLighting.glsl"
#include "_PixelLighting.glsl"
#include "_Shadow.glsl"

#include "_Material.glsl"
#include "_Fog.glsl"

VERTEX_OUTPUT(float vWorldDepth)

VERTEX_OUTPUT(vec2 vTexCoord)
#ifdef URHO3D_HAS_LIGHTMAP
    VERTEX_OUTPUT(vec2 vTexCoord2)
#endif

#ifdef URHO3D_VERTEX_HAS_COLOR
    VERTEX_OUTPUT(vec4 vColor)
#endif

VERTEX_OUTPUT(vec3 vNormal)
VERTEX_OUTPUT(vec3 vLightVec)
#ifdef URHO3D_NEED_EYE_VECTOR
    VERTEX_OUTPUT(vec3 vEyeVec)
#endif
#ifdef URHO3D_NORMAL_MAPPING
    VERTEX_OUTPUT(vec4 vTangent)
    VERTEX_OUTPUT(vec2 vBitangentXY)
#endif

#ifdef URHO3D_AMBIENT_PASS
    VERTEX_OUTPUT(vec3 vAmbientAndVertexLigthing)
#endif
#ifdef URHO3D_HAS_SHADOW
    VERTEX_OUTPUT(optional_highp vec4 vShadowPos[URHO3D_SHADOW_NUM_CASCADES])
#endif

#ifdef URHO3D_LIGHT_CUSTOM_SHAPE
    VERTEX_OUTPUT(vec4 vShapePos)
#endif

#ifdef URHO3D_VERTEX_SHADER
void main()
{
    VertexTransform vertexTransform = GetVertexTransform();
    gl_Position = WorldToClipSpace(vertexTransform.position.xyz);
    vWorldDepth = GetDepth(gl_Position);

    vTexCoord = GetTransformedTexCoord();
    vNormal = vertexTransform.normal;
    vLightVec = GetLightVector(vertexTransform.position.xyz);
    #ifdef URHO3D_NEED_EYE_VECTOR
        vEyeVec = GetEyeVector(vertexTransform.position.xyz);
    #endif

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

    #ifdef URHO3D_AMBIENT_PASS
        vAmbientAndVertexLigthing = GetAmbientAndVertexLights(vertexTransform.position.xyz, vertexTransform.normal);
    #endif

    #ifdef URHO3D_HAS_SHADOW
        WorldSpaceToShadowCoord(vShadowPos, vertexTransform.position);
    #endif

    #ifdef URHO3D_LIGHT_CUSTOM_SHAPE
        vShapePos = vertexTransform.position * cLightShapeMatrix;
    #endif
}
#endif

#ifdef URHO3D_PIXEL_SHADER
void main()
{
    vec4 albedo = GetMaterialAlbedo(vTexCoord.xy);

    #ifdef URHO3D_HAS_SPECULAR_HIGHLIGHTS
        vec3 specColor = GetMaterialSpecularColor(vTexCoord.xy);
    #else
        vec3 specColor = vec3(0.0);
    #endif

    #ifdef URHO3D_VERTEX_HAS_COLOR
        albedo *= vColor;
    #endif

    #ifdef URHO3D_NORMAL_MAPPING
        vec3 normal = GetMaterialNormal(vTexCoord.xy, vNormal, vTangent, vBitangentXY);
    #else
        vec3 normal = GetMaterialNormal(vNormal);
    #endif

    float fogFactor = GetFogFactor(vWorldDepth);

    #ifndef DEFERRED
        vec4 lightVec = NormalizeLightVector(vLightVec);
        float diff = GetDiffuseIntensity(normal, lightVec.xyz, lightVec.w);

        #ifdef SHADOW
            diff *= GetForwardShadow(vShadowPos, vWorldDepth);
        #endif

        #ifdef URHO3D_LIGHT_CUSTOM_SHAPE
            vec3 lightColor = GetLightColorFromShape(vShapePos);
        #else
            vec3 lightColor = GetLightColor(lightVec.xyz);
        #endif

        #ifdef URHO3D_HAS_SPECULAR_HIGHLIGHTS
            vec3 eyeVec = normalize(vEyeVec.xyz);
            float spec = GetSpecularIntensity(normal, eyeVec, lightVec.xyz, cMatSpecColor.a);
            vec3 finalColor = diff * lightColor * (albedo.rgb + spec * specColor * cLightColor.a);
        #else
            vec3 finalColor = diff * lightColor * albedo.rgb;
        #endif

        #ifdef URHO3D_AMBIENT_PASS
            #ifdef URHO3D_HAS_LIGHTMAP
                finalColor += GetAmbientLightingFromLightmap(vAmbientAndVertexLigthing, vTexCoord2, albedo.rgb);
            #else
                finalColor += GetAmbientLighting(vAmbientAndVertexLigthing, vTexCoord, albedo.rgb);
            #endif
        #endif

        gl_FragColor = vec4(ApplyFog(finalColor, fogFactor), albedo.a);
    #else
        // Fill deferred G-buffer
        float specIntensity = specColor.g;
        float specPower = cMatSpecColor.a / 255.0;

        #ifdef URHO3D_HAS_LIGHTMAP
            vec3 finalColor = GetAmbientLightingFromLightmap(vAmbientAndVertexLigthing, vTexCoord2, albedo.rgb);
        #else
            vec3 finalColor = GetAmbientLighting(vAmbientAndVertexLigthing, vTexCoord, albedo.rgb);
        #endif

        gl_FragData[0] = vec4(ApplyFog(finalColor, fogFactor), 1.0);
        gl_FragData[1] = fogFactor * vec4(albedo.rgb, specIntensity);
        gl_FragData[2] = vec4(normal * 0.5 + 0.5, specPower);
    #endif
}
#endif
