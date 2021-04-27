#include "_Config.glsl"
#include "_GammaCorrection.glsl"
#include "_BRDF.glsl"

#include "_Uniforms.glsl"
#include "_Samplers.glsl"
#include "_VertexLayout.glsl"

#include "_VertexTransform.glsl"

VERTEX_OUTPUT_HIGHP(vec2 vTexCoord)
#ifdef URHO3D_VERTEX_HAS_COLOR
    VERTEX_OUTPUT(half4 vColor)
#endif

#ifdef URHO3D_VERTEX_SHADER
void main()
{
    VertexTransform vertexTransform = GetVertexTransform();
    gl_Position = WorldToClipSpace(vertexTransform.position.xyz);
    ApplyClipPlane(gl_Position);

    #ifdef URHO3D_VERTEX_HAS_TEXCOORD0
        vTexCoord = iTexCoord;
    #else
        vTexCoord = vec2(0.0);
    #endif

    #ifdef URHO3D_VERTEX_HAS_COLOR
        vColor = iColor;
    #endif
}
#endif

#ifdef URHO3D_PIXEL_SHADER
void main()
{
    half4 diffColor = cMatDiffColor;

    #ifdef URHO3D_VERTEX_HAS_COLOR
        diffColor *= vColor;
    #endif

    #ifdef URHO3D_MATERIAL_HAS_DIFFUSE
        #ifdef ALPHAMAP
            half alphaInput = DecodeAlphaMap(texture2D(sDiffMap, vTexCoord));
            diffColor.a *= alphaInput;
        #else
            half4 diffInput = texture2D(sDiffMap, vTexCoord);
            #ifdef ALPHAMASK
                if (diffInput.a < 0.5)
                    discard;
            #endif
            diffColor *= diffInput;
        #endif
    #endif

    gl_FragColor = GammaToLightSpaceAlpha(diffColor);
}
#endif
