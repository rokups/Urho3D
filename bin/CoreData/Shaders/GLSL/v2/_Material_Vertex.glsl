/// _Material_Vertex.glsl
/// Don't include!
/// Material vertex shader implementation.

/// Fill common vertex shader outputs for material.
void FillCommonVertexOutput(VertexTransform vertexTransform, vec2 uv)
{
    gl_Position = WorldToClipSpace(vertexTransform.position.xyz);
    vWorldDepth = GetDepth(gl_Position);
    vTexCoord = uv;

#ifdef URHO3D_PIXEL_NEED_VERTEX_COLOR
    vColor = iColor;
#endif

#ifdef URHO3D_PIXEL_NEED_SCREEN_POSITION
    vScreenPos = GetScreenPos(gl_Position);
#endif

#ifdef URHO3D_PIXEL_NEED_NORMAL
    vNormal = vertexTransform.normal;
#endif

#ifdef URHO3D_PIXEL_NEED_TANGENT
    vTangent = cNormalScale * vec4(vertexTransform.tangent.xyz, vertexTransform.bitangent.z);
    vBitangentXY = cNormalScale * vertexTransform.bitangent.xy;
#endif

#ifdef URHO3D_PIXEL_NEED_EYE_VECTOR
    vEyeVec = cCameraPos - vertexTransform.position.xyz;
#endif

#ifdef URHO3D_PIXEL_NEED_AMBIENT
    vAmbientAndVertexLigthing = GetAmbientAndVertexLights(vertexTransform.position.xyz, vertexTransform.normal);
#endif

#ifdef URHO3D_IS_LIT

#ifdef URHO3D_HAS_LIGHTMAP
    vTexCoord2 = GetLightMapTexCoord();
#endif

#ifdef URHO3D_HAS_PIXEL_LIGHT
    vLightVec = GetLightVector(vertexTransform.position.xyz);
#endif

#ifdef URHO3D_HAS_SHADOW
    WorldSpaceToShadowCoord(vShadowPos, vertexTransform.position);
#endif

#ifdef URHO3D_LIGHT_CUSTOM_SHAPE
    vShapePos = vertexTransform.position * cLightShapeMatrix;
#endif

#endif // URHO3D_IS_LIT
}
