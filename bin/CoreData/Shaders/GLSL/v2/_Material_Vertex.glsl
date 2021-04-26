/// _Material_Vertex.glsl
/// Don't include!
/// Material vertex shader implementation.

/// Fill vertex transform attributes:
/// - gl_Position
/// - vWorldDepth
/// - vNormal
/// - vTangent
/// - vBitangentXY
void FillVertexTransformOutputs(const VertexTransform vertexTransform)
{
    gl_Position = WorldToClipSpace(vertexTransform.position.xyz);
    vWorldDepth = GetDepth(gl_Position);

#ifdef URHO3D_PIXEL_NEED_NORMAL
    vNormal = vertexTransform.normal;
#endif

#ifdef URHO3D_PIXEL_NEED_TANGENT
    vTangent = cNormalScale * vec4(vertexTransform.tangent.xyz, vertexTransform.bitangent.z);
    vBitangentXY = cNormalScale * vertexTransform.bitangent.xy;
#endif
}

/// Fill texcoord attributes:
/// - vTexCoord
/// - vTexCoord2
/// - vColor
void FillTexCoordOutputs()
{
    vTexCoord = GetTransformedTexCoord();

#ifdef URHO3D_PIXEL_NEED_LIGHTMAP_UV
    vTexCoord2 = GetLightMapTexCoord();
#endif

#ifdef URHO3D_PIXEL_NEED_VERTEX_COLOR
    vColor = iColor;
#endif
}

/// Fill lighting attributes:
/// - vAmbientAndVertexLigthing
/// - vLightVec
/// - vShadowPos
/// - vShapePos
void FillLightOutputs(const VertexTransform vertexTransform)
{
#ifdef URHO3D_SURFACE_NEED_AMBIENT
    vAmbientAndVertexLigthing = GetAmbientAndVertexLights(vertexTransform.position.xyz, vertexTransform.normal);
#endif

#ifdef URHO3D_PIXEL_NEED_LIGHT_VECTOR
    vLightVec = GetLightVector(vertexTransform.position.xyz);
#endif

#ifdef URHO3D_PIXEL_NEED_SHADOW_POS
    WorldSpaceToShadowCoord(vShadowPos, vertexTransform.position);
#endif

#ifdef URHO3D_PIXEL_NEED_LIGHT_SHAPE_POS
    vShapePos = vertexTransform.position * cLightShapeMatrix;
#endif
}

/// Fill position at render target:
/// - vScreenPos
#ifdef URHO3D_PIXEL_NEED_SCREEN_POSITION
    #define FillScreenPosOutput(clipPos) \
        vScreenPos = GetScreenPos(clipPos)
#else
    #define FillScreenPosOutput(clipPos)
#endif

/// Fill eye vector:
/// - vEyeVec
#ifdef URHO3D_PIXEL_NEED_EYE_VECTOR
    #define FillEyeVectorOutput(worldPos) \
        vEyeVec = cCameraPos - worldPos
#else
    #define FillEyeVectorOutput(worldPos)
#endif

/// Fill reflection vector:
/// - vReflectionVec
#ifdef URHO3D_PIXEL_NEED_REFLECTION_VECTOR
    #ifdef URHO3D_PIXEL_NEED_EYE_VECTOR
        #define FillReflectionVectorOutput(worldPos) \
            vReflectionVec = reflect(-vEyeVec, vNormal)
    #else
        #define FillReflectionVectorOutput(worldPos) \
            vReflectionVec = reflect(worldPos - cCameraPos, vNormal)
    #endif
#else
    #define FillReflectionVectorOutput(worldPos)
#endif

/// Fill eye and/or reflection vector:
/// - vEyeVec
/// - vReflectionVec
#define FillEyeAndReflectionVectorOutputs(worldPos) \
    FillEyeVectorOutput(worldPos); \
    FillReflectionVectorOutput(worldPos)

/// Fill all abovementioned outputs.
#define FillVertexOutputs(vertexTransform) \
    FillVertexTransformOutputs(vertexTransform); \
    FillTexCoordOutputs(); \
    FillLightOutputs(vertexTransform); \
    FillScreenPosOutput(gl_Position); \
    FillEyeAndReflectionVectorOutputs(vertexTransform.position.xyz) \
    ApplyClipPlane(gl_Position)
