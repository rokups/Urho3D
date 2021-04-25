#include "Uniforms.glsl"
#include "Samplers.glsl"
#include "Transform.glsl"

#if defined(DIFFMAP) || defined(ALPHAMAP)
    varying vec2 vTexCoord;
#endif
#ifdef VERTEXCOLOR
    varying vec4 vColor;
#endif

void VS()
{
    mat4 modelMatrix = iModelMatrix;
    vec3 worldPos = GetWorldPos(modelMatrix);
    gl_Position = GetClipPos(worldPos);

    #ifdef DIFFMAP
        vTexCoord = iTexCoord;
    #endif
    #ifdef VERTEXCOLOR
        vColor = iColor;
    #endif
}

void PS()
{
    vec4 diffColor = cMatDiffColor;

    #ifdef VERTEXCOLOR
        diffColor *= vColor;
    #endif

    #ifdef DIFFMAP
        vec4 diffInput = texture2D(sDiffMap, vTexCoord);
        #ifdef ALPHAMASK
            if (diffInput.a < 0.5)
                discard;
        #endif
        diffColor *= diffInput;
    #endif
    #ifdef ALPHAMAP
        #if defined(GL3) && defined(DESKTOP_GRAPHICS)
            float alphaInput = texture2D(sDiffMap, vTexCoord).r;
        #else
            float alphaInput = texture2D(sDiffMap, vTexCoord).a;
        #endif
        diffColor.a *= alphaInput;
    #endif

    #ifdef URHO3D_LINEAR_OUTPUT
        diffColor.rgb *= diffColor.rgb * (diffColor.rgb * 0.305306011 + 0.682171111) + 0.012522878;
    #endif
    gl_FragColor = diffColor;
}
