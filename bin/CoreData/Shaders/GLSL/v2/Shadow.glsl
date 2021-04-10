#include "_Config.glsl"
#include "_Uniforms.glsl"
#include "_VertexLayout.glsl"
#include "_VertexTransform.glsl"
#include "_Samplers.glsl"

#ifdef VSM_SHADOW
    VERTEX_OUTPUT(vec4 vTexCoord)
#else
    VERTEX_OUTPUT(vec2 vTexCoord)
#endif

#ifdef URHO3D_VERTEX_SHADER
void main()
{
    VertexTransform vertexTransform = GetVertexTransform();
    //mat4 modelMatrix = iModelMatrix;
    vec3 worldPos = vertexTransform.position.xyz;// GetWorldPos(modelMatrix);
    gl_Position = GetClipPos(worldPos);

    vTexCoord.xy = GetTransformedTexCoord();

    #ifdef VSM_SHADOW
        vTexCoord.zw = gl_Position.zw;
    #endif
}
#endif

#ifdef URHO3D_PIXEL_SHADER
void main()
{
    #ifdef ALPHAMASK
        float alpha = texture2D(sDiffMap, vTexCoord.xy).a;
        if (alpha < 0.5)
            discard;
    #endif

    #ifdef VSM_SHADOW
        #ifdef D3D11
            float depth = vTexCoord.z / vTexCoord.w;
        #else
            float depth = vTexCoord.z / vTexCoord.w * 0.5 + 0.5;
        #endif
        gl_FragColor = vec4(depth, depth * depth, 1.0, 1.0);
    #else
        gl_FragColor = vec4(1.0);
    #endif
}
#endif
