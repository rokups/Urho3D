#include "_Config.glsl"
#include "_Uniforms.glsl"
#include "_VertexLayout.glsl"
#include "_VertexTransform.glsl"
#include "_PixelOutput.glsl"
#include "Samplers.glsl"
#include "ScreenPos.glsl"
#include "Lighting.glsl"
#include "Fog.glsl"

#ifdef NORMALMAP
    VERTEX_SHADER_OUT(vec4 vTexCoord)
    VERTEX_SHADER_OUT(vec4 vTangent)
#else
    VERTEX_SHADER_OUT(vec2 vTexCoord)
#endif
VERTEX_SHADER_OUT(vec3 vNormal)
VERTEX_SHADER_OUT(vec4 vWorldPos)
#ifdef VERTEXCOLOR
    VERTEX_SHADER_OUT(vec4 vColor)
#endif
#if defined(LIGHTMAP) || defined(AO)
    VERTEX_SHADER_OUT(vec2 vTexCoord2)
#endif
VERTEX_SHADER_OUT(vec3 vVertexLight)
#ifdef PERPIXEL
    #ifdef SHADOW
        #ifndef GL_ES
            VERTEX_SHADER_OUT(vec4 vShadowPos[NUMCASCADES])
        #else
            VERTEX_SHADER_OUT(highp vec4 vShadowPos[NUMCASCADES])
        #endif
    #endif
    #ifdef SPOTLIGHT
        VERTEX_SHADER_OUT(vec4 vSpotPos)
    #endif
    #ifdef POINTLIGHT
        VERTEX_SHADER_OUT(vec3 vCubeMaskVec)
    #endif
#else
    VERTEX_SHADER_OUT(vec4 vScreenPos)
    #ifdef ENVCUBEMAP
        VERTEX_SHADER_OUT(vec3 vReflectionVec)
    #endif
#endif

#ifdef STAGE_VERTEX_SHADER
void main()
{
    mat4 modelMatrix = iModelMatrix;
    vec3 worldPos = GetWorldPos(modelMatrix);
    gl_Position = GetClipPos(worldPos);
    vNormal = GetWorldNormal(modelMatrix);
    vWorldPos = vec4(worldPos, GetDepth(gl_Position));

    #ifdef VERTEXCOLOR
        vColor = iColor;
    #endif

    #ifdef NOUV
        vTexCoord.xy = vec2(0.0, 0.0);
    #else
        vTexCoord.xy = GetTexCoord(iTexCoord);
    #endif

    #ifdef NORMALMAP
        vec4 tangent = GetWorldTangent(modelMatrix);
        vec3 bitangent = cross(tangent.xyz, vNormal) * tangent.w;
        vTexCoord.zw = bitangent.xy;
        vTangent = vec4(tangent.xyz, bitangent.z);
    #endif

    #if defined(PASS_BASE_LITBASE) || defined(PASS_ALPHA_LITBASE)
        vVertexLight = GetAmbientLight(vec4(vNormal, 1)) + cAmbientColor.rgb;

        #ifdef NUMVERTEXLIGHTS
            for (int i = 0; i < NUMVERTEXLIGHTS; ++i)
                vVertexLight += GetVertexLight(i, worldPos, vNormal) * cVertexLights[i * 3].rgb;
        #endif
    #endif

    vec4 projWorldPos = vec4(worldPos, 1.0);
    #ifdef SHADOW
        // Shadow projection: transform from world space to shadow space
        for (int i = 0; i < NUMCASCADES; i++)
            vShadowPos[i] = GetShadowPos(i, vNormal, projWorldPos);
    #endif

    #ifdef SPOTLIGHT
        vSpotPos = projWorldPos * cLightMatrices[0];
    #endif

    #ifdef POINTLIGHT
        vCubeMaskVec = (worldPos - cLightPos.xyz) * mat3(cLightMatrices[0][0].xyz, cLightMatrices[0][1].xyz, cLightMatrices[0][2].xyz);
    #endif
}
#endif

#ifdef STAGE_PIXEL_SHADER
void main()
{
    // Get material diffuse albedo
    #ifdef DIFFMAP
        vec4 diffInput = texture2D(sDiffMap, vTexCoord.xy);
        #ifdef ALPHAMASK
            if (diffInput.a < 0.5)
                discard;
        #endif
        vec4 diffColor = cMatDiffColor * diffInput;
    #else
        vec4 diffColor = cMatDiffColor;
    #endif

    #ifdef VERTEXCOLOR
        diffColor *= vColor;
    #endif

    // Get material specular albedo
    #ifdef SPECMAP
        vec3 specColor = cMatSpecColor.rgb * texture2D(sSpecMap, vTexCoord.xy).rgb;
    #else
        vec3 specColor = cMatSpecColor.rgb;
    #endif

    // Get normal
    #ifdef NORMALMAP
        mat3 tbn = mat3(vTangent.xyz, vec3(vTexCoord.zw, vTangent.w), vNormal);
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
            diff *= GetShadow(vShadowPos, vWorldPos.w);
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

        #ifdef AMBIENT
            finalColor += vVertexLight * diffColor.rgb;
            #ifdef LIGHTMAP
                finalColor += (texture2D(sEmissiveMap, vTexCoord2).rgb * 2.0 + cAmbientColor.rgb) * diffColor.rgb;
            #elif defined(EMISSIVEMAP)
                finalColor += cMatEmissiveColor * texture2D(sEmissiveMap, vTexCoord.xy).rgb;
            #else
                finalColor += cMatEmissiveColor;
            #endif
            gl_FragColor = vec4(GetFog(finalColor, fogFactor), diffColor.a);
        #else
            gl_FragColor = vec4(GetLitFog(finalColor, fogFactor), diffColor.a);
        #endif

        gl_FragColor = vec4(GetFog(finalColor, fogFactor), diffColor.a);
    #endif
}
#endif
