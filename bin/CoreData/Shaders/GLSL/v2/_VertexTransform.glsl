#ifndef _VERTEX_TRANSFORM_GLSL_
#define _VERTEX_TRANSFORM_GLSL_

#ifndef _VERTEX_LAYOUT_GLSL_
    #error Include "_VertexLayout.glsl" before "_VertexTransform.glsl"
#endif

#ifdef URHO3D_VERTEX_SHADER

// Fake normal and tangent defines for some geometry types because they are calculated
#if defined(URHO3D_GEOMETRY_BILLBOARD) || defined(URHO3D_GEOMETRY_DIRBILLBOARD) || defined(URHO3D_GEOMETRY_TRAIL_FACE_CAMERA) || defined(URHO3D_GEOMETRY_TRAIL_BONE)
    #ifndef URHO3D_VERTEX_HAS_NORMAL
        #define URHO3D_VERTEX_HAS_NORMAL
    #endif
    #ifndef URHO3D_VERTEX_HAS_TANGENT
        #define URHO3D_VERTEX_HAS_TANGENT
    #endif
#endif

// URHO3D_USE_NORMAL_MAPPING: Whether to apply normal mapping
#if defined(NORMALMAP) && defined(URHO3D_VERTEX_HAS_NORMAL) && defined(URHO3D_VERTEX_HAS_TANGENT) //&& defined(URHO3D_MATERIAL_HAS_NORMAL)
    #define URHO3D_USE_NORMAL_MAPPING
#endif

// URHO3D_IGNORE_NORMAL: Whether shader is not interested in vertex normal
// TODO(renderer): Check for unlit here
#if !defined(URHO3D_VERTEX_HAS_NORMAL)
    #define URHO3D_IGNORE_NORMAL
#endif

// URHO3D_IGNORE_TANGENT: Whether shader is not interested in vertex tangent
#if defined(URHO3D_IGNORE_NORMAL) || !defined(URHO3D_VERTEX_HAS_TANGENT) || !defined(URHO3D_USE_NORMAL_MAPPING)
    #define URHO3D_IGNORE_TANGENT
#endif

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
    return vec4(worldPos, 1.0) * cViewProj;
}

/// Return depth from position in clip space.
float GetDepth(vec4 clipPos)
{
    return dot(clipPos.zw, cDepthMode.zw);
}

/// Vertex data in world space
struct VertexTransform
{
    /// Vertex position in world space
    vec3 position;
#ifndef URHO3D_IGNORE_NORMAL
    /// Vertex normal in world space
    vec3 normal;
#endif
#ifndef URHO3D_IGNORE_TANGENT
    /// Vertex tangent in world space with binormal direction sign as last component
    vec4 tangent;
#endif
};

/// Return model-to-world space matrix.
#if defined(URHO3D_GEOMETRY_SKINNED)
    mat4 GetModelMatrix()
    {
        ivec4 idx = ivec4(iBlendIndices) * 3;

        #define GetSkinMatrixColumn(i1, i2, i3, i4, k) (cSkinMatrices[i1] * k.x + cSkinMatrices[i2] * k.y + cSkinMatrices[i3] * k.z + cSkinMatrices[i4] * k.w)
        vec4 col1 = GetSkinMatrixColumn(idx.x, idx.y, idx.z, idx.w, iBlendWeights);
        vec4 col2 = GetSkinMatrixColumn(idx.x + 1, idx.y + 1, idx.z + 1, idx.w + 1, iBlendWeights);
        vec4 col3 = GetSkinMatrixColumn(idx.x + 2, idx.y + 2, idx.z + 2, idx.w + 2, iBlendWeights);
        #undef GetSkinMatrixColumn

        const vec4 col4 = vec4(0.0, 0.0, 0.0, 1.0);
        return mat4(col1, col2, col3, col4);
    }
#else
    #define GetModelMatrix() cModel
#endif

/// Return vertex transform in world space.
///
/// URHO3D_GEOMETRY_STATIC, URHO3D_GEOMETRY_SKINNED:
///   iPos: Vertex position in model space
///   iNormal: (optional) Vertex normal in model space
///   iTangent: (optional) Vertex tangent in model space and sign of binormal
///
/// URHO3D_GEOMETRY_BILLBOARD:
///   iPos: Billboard position in model space
///   iTexCoord1: Billboard size
///
/// URHO3D_GEOMETRY_DIRBILLBOARD:
///   iPos: Billboard position in model space
///   iNormal: Billboard normal in model space
///   iTexCoord1: Billboard size
///
/// URHO3D_GEOMETRY_TRAIL_FACE_CAMERA:
///
/// iTangent(URHO3D_GEOMETRY_TRAIL_FACE_CAMERA): Trail direction in model space and trail scale
/// iTangent(URHO3D_GEOMETRY_TRAIL_BONE): Parent position in world space and trail scale
/// iTexCoord1(URHO3D_GEOMETRY_BILLBOARD, URHO3D_GEOMETRY_DIRBILLBOARD): Size of bilboard
#if defined(URHO3D_GEOMETRY_STATIC) || defined(URHO3D_GEOMETRY_SKINNED)
    VertexTransform GetVertexTransform()
    {
        mat4 modelMatrix = GetModelMatrix();

        VertexTransform result;
        result.position = (iPos * modelMatrix).xyz;

        #ifndef URHO3D_IGNORE_NORMAL
            mat3 normalMatrix = GetNormalMatrix(modelMatrix);
            result.normal = normalize(iNormal * normalMatrix);

            #ifndef URHO3D_IGNORE_TANGENT
                result.tangent = vec4(normalize(iTangent.xyz * normalMatrix), iTangent.w);
            #endif
        #endif

        return result;
    }
#elif defined(URHO3D_GEOMETRY_BILLBOARD)
    VertexTransform GetVertexTransform()
    {
        mat4 modelMatrix = GetModelMatrix();

        VertexTransform result;
        result.position = (iPos * modelMatrix).xyz;
        result.position += vec3(iTexCoord1.x, iTexCoord1.y, 0.0) * cBillboardRot;

        #ifndef URHO3D_IGNORE_NORMAL
            mat3 normalMatrix = GetNormalMatrix(modelMatrix);
            result.normal = vec3(-cBillboardRot[0][2], -cBillboardRot[1][2], -cBillboardRot[2][2]);

            #ifndef URHO3D_IGNORE_TANGENT
                result.tangent = vec4(normalize(vec3(1.0, 0.0, 0.0) * cBillboardRot), 1.0);
            #endif
        #endif
        return result;
    }
#elif defined(URHO3D_GEOMETRY_DIRBILLBOARD)
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

    VertexTransform GetVertexTransform()
    {
        mat4 modelMatrix = GetModelMatrix();

        VertexTransform result;
        result.position = (iPos * modelMatrix).xyz;
        mat3 rotation = GetFaceCameraRotation(result.position, iNormal);
        result.position += vec3(iTexCoord1.x, 0.0, iTexCoord1.y) * rotation;

        #ifndef URHO3D_IGNORE_NORMAL
            mat3 normalMatrix = GetNormalMatrix(modelMatrix);
            result.normal = vec3(0.0, 1.0, 0.0) * rotation;

            #ifndef URHO3D_IGNORE_TANGENT
                // TODO(renderer): Revisit
                result.tangent = vec4(normalize(vec3(1.0, 0.0, 0.0) * GetNormalMatrix(modelMatrix)), 1.0);
            #endif
        #endif
        return result;
    }
#elif defined(URHO3D_GEOMETRY_TRAIL_FACE_CAMERA)
    VertexTransform GetVertexTransform()
    {
        mat4 modelMatrix = GetModelMatrix();
        vec3 up = normalize(cCameraPos - iPos.xyz);
        vec3 right = normalize(cross(iTangent.xyz, up));

        VertexTransform result;
        result.position = (vec4((iPos.xyz + right * iTangent.w), 1.0) * modelMatrix).xyz;

        #ifndef URHO3D_IGNORE_NORMAL
            result.normal = up;

            #ifndef URHO3D_IGNORE_TANGENT
                result.tangent = vec4(iTangent.xyz, 1.0);
            #endif
        #endif

        return result;
    }
#elif defined(URHO3D_GEOMETRY_TRAIL_BONE)
    VertexTransform GetVertexTransform()
    {
        mat4 modelMatrix = GetModelMatrix();
        vec3 right = iTangent.xyz - iPos.xyz;
        vec3 up = normalize(cross(normalize(iNormal), right));

        VertexTransform result;
        result.position = (vec4((iPos.xyz + right * iTangent.w), 1.0) * modelMatrix).xyz;

        #ifndef URHO3D_IGNORE_NORMAL
            mat3 normalMatrix = GetNormalMatrix(modelMatrix);
            result.normal = up;

            #ifndef URHO3D_IGNORE_TANGENT
                result.tangent = vec4(normalize(iNormal), 1.0);
            #endif
        #endif

        return result;
    }
#endif

#endif

#endif
