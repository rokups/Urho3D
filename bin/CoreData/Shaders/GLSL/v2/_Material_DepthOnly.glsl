/// _Material_Pixel_DepthOnly.glsl
/// Don't include!
/// Default depth-only vertex and pixel shaders.

VERTEX_OUTPUT_HIGHP(vec2 vTexCoord)
#ifdef URHO3D_VARIANCE_SHADOW_MAP
    VERTEX_OUTPUT_HIGHP(vec2 vDepth)
#endif

#ifdef URHO3D_VERTEX_SHADER
void FillVertexOutputs(const VertexTransform vertexTransform)
{
    gl_Position = WorldToClipSpace(vertexTransform.position.xyz);
    ApplyClipPlane(gl_Position);
    vTexCoord = GetTransformedTexCoord();
#ifdef URHO3D_VARIANCE_SHADOW_MAP
    vDepth = gl_Position.zw;
#endif
}
#endif // URHO3D_VERTEX_SHADER

#ifdef URHO3D_PIXEL_SHADER
void DefaultPixelShader()
{
#ifdef ALPHAMASK
    fixed alpha = texture2D(sDiffMap, vTexCoord.xy).a;
    if (alpha < 0.5)
        discard;
#endif

#ifdef URHO3D_VARIANCE_SHADOW_MAP
    float depth = vDepth.x / vDepth.y;
    #ifndef D3D11
        // Remap from [-1, 1] to [0, 1] for OpenGL
        depth = depth * 0.5 + 0.5;
    #endif
    gl_FragColor = vec4(depth, depth * depth, 1.0, 1.0);
#endif
}
#endif // URHO3D_PIXEL_SHADER
