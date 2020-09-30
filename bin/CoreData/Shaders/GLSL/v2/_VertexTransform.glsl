#ifndef _VERTEX_TRANSFORM_GLSL_
#define _VERTEX_TRANSFORM_GLSL_

#ifdef STAGE_VERTEX_SHADER

/// Return normal matrix from model matrix.
mat3 GetNormalMatrix(mat4 modelMatrix)
{
    return mat3(modelMatrix[0].xyz, modelMatrix[1].xyz, modelMatrix[2].xyz);
}

/// Return transformed tex coords.
vec2 GetTexCoord(vec2 texCoord)
{
    return vec2(dot(texCoord, cUOffset.xy) + cUOffset.w, dot(texCoord, cVOffset.xy) + cVOffset.w);
}

/// Return transformed lightmap tex coords.
vec2 GetLightMapTexCoord(vec2 texCoord)
{
    return texCoord * cLMOffset.xy + cLMOffset.zw;
}

/// Return position in clip space from position in world space.
vec4 GetClipPos(vec3 worldPos)
{
    vec4 ret = vec4(worldPos, 1.0) * cViewProj;
    // While getting the clip coordinate, also automatically set gl_ClipVertex for user clip planes
    // TODO(renderer): Implement clip planes
/*#if !defined(GL_ES)
#   if !defined(GL3)
    gl_ClipVertex = ret;
#   else
    gl_ClipDistance[0] = dot(cClipPlane, ret);
#   endif
#endif*/
    return ret;
}

/// Return depth from position in clip space.
float GetDepth(vec4 clipPos)
{
    return dot(clipPos.zw, cDepthMode.zw);
}

/// Return model matrix.
#if defined(GEOM_SKINNED)
    mat4 GetSkinMatrix(vec4 blendWeights, ivec4 blendIndices)
    {
        #define GetSkinMatrixColumn(i1, i2, i3, i4, k) (cSkinMatrices[i1] * k.x + cSkinMatrices[i2] * k.y + cSkinMatrices[i3] * k.z + cSkinMatrices[i4] * k.w)
        ivec4 idx = blendIndices * 3;
        vec4 col1 = GetSkinMatrixColumn(idx.x, idx.y, idx.z, idx.w, blendWeights);
        vec4 col2 = GetSkinMatrixColumn(idx.x + 1, idx.y + 1, idx.z + 1, idx.w + 1, blendWeights);
        vec4 col3 = GetSkinMatrixColumn(idx.x + 2, idx.y + 2, idx.z + 2, idx.w + 2, blendWeights);
        const vec4 col4 = vec4(0.0, 0.0, 0.0, 1.0);
        return mat4(col1, col2, col3, col4);
        #undef GetSkinMatrixColumn
    }
    #define GetModelMatrix() GetSkinMatrix(iBlendWeights, ivec4(iBlendIndices))
#elif defined(GEOM_INSTANCED)
    #define GetModelMatrix() mat4(iTexCoord4, iTexCoord5, iTexCoord6, vec4(0.0, 0.0, 0.0, 1.0))
#else
    #define GetModelMatrix() cModel
#endif

// TODO(renderer): Deprecated
#define iModelMatrix GetModelMatrix()

#ifdef LAYOUT_HAS_POSITION
/// Return world position for simple geometry.
vec3 GetGeometryPos(mat4 modelMatrix)
{
    return (iPos * modelMatrix).xyz;
}
#endif

#ifdef LAYOUT_HAS_NORMAL
/// Return world normal for simple geometry.
vec3 GetGeometryNormal(mat4 modelMatrix)
{
    return normalize(iNormal * GetNormalMatrix(modelMatrix));
}
#endif

#ifdef LAYOUT_HAS_TANGENT
/// Return world tangent for simple geometry.
vec4 GetGeometryTangent(mat4 modelMatrix)
{
    return vec4(normalize(iTangent.xyz * GetNormalMatrix(modelMatrix)), iTangent.w);
}
#endif

/// Return world position/normal/tangent for current geometry type.
#if defined(GEOM_STATIC) || defined(GEOM_SKINNED) || defined(GEOM_INSTANCED)
    #define GetWorldPos(modelMatrix) GetGeometryPos(modelMatrix)
    #define GetWorldNormal(modelMatrix) GetGeometryNormal(modelMatrix)
    #define GetWorldTangent(modelMatrix) GetGeometryTangent(modelMatrix)
#elif defined(GEOM_BILLBOARD)
    vec3 GetBillboardPos(vec4 position, vec2 size, mat4 modelMatrix)
    {
        return (position * modelMatrix).xyz + vec3(size.x, size.y, 0.0) * cBillboardRot;
    }
    vec3 GetBillboardNormal()
    {
        return vec3(-cBillboardRot[0][2], -cBillboardRot[1][2], -cBillboardRot[2][2]);
    }
    vec4 GetBilboardTangent()
    {
        return vec4(normalize(vec3(1.0, 0.0, 0.0) * cBillboardRot), 1.0);
    }
    #define GetWorldPos(modelMatrix) GetBillboardPos(iPos, iTexCoord1, modelMatrix);
    #define GetWorldNormal(modelMatrix) GetBillboardNormal()
    #define GetWorldTangent(modelMatrix) GetBilboardTangent()
#elif defined(GEOM_DIRBILLBOARD)
    mat3 GetFaceCameraRotation(vec3 position, vec3 direction)
    {
        vec3 cameraDir = normalize(position - cCameraPos);
        vec3 front = normalize(direction);
        vec3 right = normalize(cross(front, cameraDir));
        vec3 up = normalize(cross(front, right));

        return mat3(
            right.x, up.x, front.x,
            right.y, up.y, front.y,
            right.z, up.z, front.z
        );
    }
    vec3 GetBillboardPos(vec4 iPos, vec3 iDirection, mat4 modelMatrix)
    {
        vec3 worldPos = (iPos * modelMatrix).xyz;
        return worldPos + vec3(iTexCoord1.x, 0.0, iTexCoord1.y) * GetFaceCameraRotation(worldPos, iDirection);
    }
    vec3 GetBillboardNormal(vec4 iPos, vec3 iDirection, mat4 modelMatrix)
    {
        vec3 worldPos = (iPos * modelMatrix).xyz;
        return vec3(0.0, 1.0, 0.0) * GetFaceCameraRotation(worldPos, iDirection);
    }
    vec4 GetBilboardTangent(mat4 modelMatrix)
    {
        return vec4(normalize(vec3(1.0, 0.0, 0.0) * GetNormalMatrix(modelMatrix)), 1.0);
    }
    #define GetWorldPos(modelMatrix) GetBillboardPos(iPos, iNormal, modelMatrix);
    #define GetWorldNormal(modelMatrix) GetBillboardNormal(iPos, iNormal, modelMatrix)
    #define GetWorldTangent(modelMatrix) GetBilboardTangent(modelMatrix)
#elif defined(GEOM_TRAIL_FACE_CAMERA)
    vec3 GetTrailPos(vec4 iPos, vec3 iFront, float iScale, mat4 modelMatrix)
    {
        vec3 up = normalize(cCameraPos - iPos.xyz);
        vec3 right = normalize(cross(iFront, up));
        return (vec4((iPos.xyz + right * iScale), 1.0) * modelMatrix).xyz;
    }
    vec3 GetTrailNormal(vec4 iPos)
    {
        return normalize(cCameraPos - iPos.xyz);
    }
    #define GetWorldPos(modelMatrix) GetTrailPos(iPos, iTangent.xyz, iTangent.w, modelMatrix)
    #define GetWorldNormal(modelMatrix) GetTrailNormal(iPos)
    #define GetWorldTangent(modelMatrix) GetGeometryTangent(modelMatrix)
#elif defined(GEOM_TRAIL_BONE)
    vec3 GetTrailPos(vec4 iPos, vec3 iParentPos, float iScale, mat4 modelMatrix)
    {
        vec3 right = iParentPos - iPos.xyz;
        return (vec4((iPos.xyz + right * iScale), 1.0) * modelMatrix).xyz;
    }

    vec3 GetTrailNormal(vec4 iPos, vec3 iParentPos, vec3 iForward)
    {
        vec3 left = normalize(iPos.xyz - iParentPos);
        vec3 up = normalize(cross(normalize(iForward), left));
        return up;
    }
    #define GetWorldPos(modelMatrix) GetTrailPos(iPos, iTangent.xyz, iTangent.w, modelMatrix)
    #define GetWorldNormal(modelMatrix) GetTrailNormal(iPos, iTangent.xyz, iNormal)
    #define GetWorldTangent(modelMatrix) GetGeometryTangent(modelMatrix)
#endif

#endif

#endif
