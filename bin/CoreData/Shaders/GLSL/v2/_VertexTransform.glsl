#ifndef _VERTEX_TRANSFORM_GLSL_
#define _VERTEX_TRANSFORM_GLSL_

#ifndef _VERTEX_LAYOUT_GLSL_
    #error Include _VertexLayout.glsl before _VertexTransform.glsl
#endif

#ifdef URHO3D_VERTEX_SHADER

/// Return normal matrix from model matrix.
mediump mat3 GetNormalMatrix(mat4 modelMatrix)
{
    return mat3(modelMatrix[0].xyz, modelMatrix[1].xyz, modelMatrix[2].xyz);
}

/// Return transformed primary UV coordinate.
vec2 GetTransformedTexCoord()
{
    #ifdef URHO3D_VERTEX_HAS_TEXCOORD0
        return vec2(dot(iTexCoord, cUOffset.xy) + cUOffset.w, dot(iTexCoord, cVOffset.xy) + cVOffset.w);
    #else
        return vec2(0.0, 0.0);
    #endif
}

#ifdef URHO3D_HAS_LIGHTMAP
    /// Return transformed secondary UV coordinate for ligthmap.
    vec2 GetLightMapTexCoord()
    {
        return iTexCoord1 * cLMOffset.xy + cLMOffset.zw;
    }
#endif

/// Return position in clip space from position in world space.
vec4 WorldToClipSpace(vec3 worldPos)
{
    return vec4(worldPos, 1.0) * cViewProj;
}

/// Clip vertex if needed.
#if defined(URHO3D_CLIP_PLANE) && !defined(GL_ES)
    #ifdef GL3
        #define ApplyClipPlane(clipPos) \
            gl_ClipDistance[0] = dot(cClipPlane, clipPos)
    #else
        #define ApplyClipPlane(clipPos) \
            gl_ClipVertex = clipPos
    #endif
#else
    #define ApplyClipPlane(clipPos)
#endif

/// Return depth from position in clip space.
float GetDepth(vec4 clipPos)
{
    return dot(clipPos.zw, cDepthMode.zw);
}

/// Vertex data in world space
struct VertexTransform
{
    /// Vertex position in world space, w = 1.0
    vec4 position;
#ifdef URHO3D_VERTEX_NEED_NORMAL
    /// Vertex normal in world space
    half3 normal;
#endif
#ifdef URHO3D_VERTEX_NEED_TANGENT
    /// Vertex tangent in world space
    half3 tangent;
    /// Vertex bitangent in world space
    half3 bitangent;
#endif
};

/// Return model-to-world space matrix.
mat4 GetModelMatrix()
{
#if defined(URHO3D_GEOMETRY_SKINNED)
    ivec4 idx = ivec4(iBlendIndices) * 3;

    #define GetSkinMatrixColumn(i1, i2, i3, i4, k) (cSkinMatrices[i1] * k.x + cSkinMatrices[i2] * k.y + cSkinMatrices[i3] * k.z + cSkinMatrices[i4] * k.w)
    vec4 col1 = GetSkinMatrixColumn(idx.x, idx.y, idx.z, idx.w, iBlendWeights);
    vec4 col2 = GetSkinMatrixColumn(idx.x + 1, idx.y + 1, idx.z + 1, idx.w + 1, iBlendWeights);
    vec4 col3 = GetSkinMatrixColumn(idx.x + 2, idx.y + 2, idx.z + 2, idx.w + 2, iBlendWeights);
    #undef GetSkinMatrixColumn

    const vec4 col4 = vec4(0.0, 0.0, 0.0, 1.0);
    return mat4(col1, col2, col3, col4);
#else
    return cModel;
#endif
}

/// Apply normal offset to position in world space.
#ifdef URHO3D_SHADOW_NORMAL_OFFSET
    void ApplyShadowNormalOffset(inout vec4 position, const half3 normal)
    {
        #ifdef URHO3D_LIGHT_DIRECTIONAL
            half3 lightDir = cLightDir;
        #else
            half3 lightDir = normalize(cLightPos.xyz - position.xyz);
        #endif
        half lightAngleCos = dot(normal, lightDir);
        half lightAngleSin = sqrt(1.0 - lightAngleCos * lightAngleCos);
        position.xyz -= normal * lightAngleSin * cNormalOffsetScale;
    }
#else
    #define ApplyShadowNormalOffset(position, normal)
#endif

/// Return vertex transform in world space. Expected vertex inputs are listed below:
///
/// URHO3D_GEOMETRY_STATIC, URHO3D_GEOMETRY_SKINNED:
///   iPos.xyz: Vertex position in model space
///   iNormal.xyz: (optional) Vertex normal in model space
///   iTangent.xyz: (optional) Vertex tangent in model space and sign of binormal
///
/// URHO3D_GEOMETRY_BILLBOARD:
///   iPos.xyz: Billboard position in model space
///   iTexCoord1.xy: Billboard size
///
/// URHO3D_GEOMETRY_DIRBILLBOARD:
///   iPos.xyz: Billboard position in model space
///   iNormal.xyz: Billboard up direction in model space
///   iTexCoord1.xy: Billboard size
///
/// URHO3D_GEOMETRY_TRAIL_FACE_CAMERA:
///   iPos.xyz: Trail position in model space
///   iTangent.xyz: Trail direction
///   iTangent.w: Trail width
///
/// URHO3D_GEOMETRY_TRAIL_BONE:
///   iPos.xyz: Trail position in model space
///   iTangent.xyz: Trail previous position in model space
///   iTangent.w: Trail width
#if defined(URHO3D_GEOMETRY_STATIC) || defined(URHO3D_GEOMETRY_SKINNED)
    VertexTransform GetVertexTransform()
    {
        mat4 modelMatrix = GetModelMatrix();

        VertexTransform result;
        result.position = iPos * modelMatrix;

        #ifdef URHO3D_VERTEX_NEED_NORMAL
            mediump mat3 normalMatrix = GetNormalMatrix(modelMatrix);
            result.normal = normalize(iNormal * normalMatrix);

            ApplyShadowNormalOffset(result.position, result.normal);

            #ifdef URHO3D_VERTEX_NEED_TANGENT
                result.tangent = normalize(iTangent.xyz * normalMatrix);
                result.bitangent = cross(result.tangent, result.normal) * iTangent.w;
            #endif
        #endif

        return result;
    }
#elif defined(URHO3D_GEOMETRY_BILLBOARD)
    VertexTransform GetVertexTransform()
    {
        mat4 modelMatrix = GetModelMatrix();

        VertexTransform result;
        result.position = iPos * modelMatrix;
        result.position.xyz += vec3(iTexCoord1.x, iTexCoord1.y, 0.0) * cBillboardRot;

        #ifdef URHO3D_VERTEX_NEED_NORMAL
            result.normal = vec3(-cBillboardRot[0][2], -cBillboardRot[1][2], -cBillboardRot[2][2]);

            ApplyShadowNormalOffset(result.position, result.normal);

            #ifdef URHO3D_VERTEX_NEED_TANGENT
                result.tangent = vec3(cBillboardRot[0][0], cBillboardRot[1][0], cBillboardRot[2][0]);
                result.bitangent = vec3(cBillboardRot[0][1], cBillboardRot[1][1], cBillboardRot[2][1]);
            #endif
        #endif
        return result;
    }
#elif defined(URHO3D_GEOMETRY_DIRBILLBOARD)
    mediump mat3 GetFaceCameraRotation(vec3 position, half3 direction)
    {
        half3 cameraDir = normalize(position - cCameraPos);
        half3 front = normalize(direction);
        half3 right = normalize(cross(front, cameraDir));
        half3 up = normalize(cross(front, right));

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
        result.position = iPos * modelMatrix;
        mediump mat3 rotation = GetFaceCameraRotation(result.position.xyz, iNormal);
        result.position.xyz += vec3(iTexCoord1.x, 0.0, iTexCoord1.y) * rotation;

        #ifdef URHO3D_VERTEX_NEED_NORMAL
            result.normal = vec3(rotation[0][1], rotation[1][1], rotation[2][1]);

            ApplyShadowNormalOffset(result.position, result.normal);

            #ifdef URHO3D_VERTEX_NEED_TANGENT
                result.tangent = vec3(rotation[0][0], rotation[1][0], rotation[2][0]);
                result.bitangent = vec3(rotation[0][2], rotation[1][2], rotation[2][2]);
            #endif
        #endif
        return result;
    }
#elif defined(URHO3D_GEOMETRY_TRAIL_FACE_CAMERA)
    VertexTransform GetVertexTransform()
    {
        mat4 modelMatrix = GetModelMatrix();
        half3 up = normalize(cCameraPos - iPos.xyz);
        half3 right = normalize(cross(iTangent.xyz, up));

        VertexTransform result;
        result.position = vec4((iPos.xyz + right * iTangent.w), 1.0) * modelMatrix;

        #ifdef URHO3D_VERTEX_NEED_NORMAL
            result.normal = normalize(cross(right, iTangent.xyz));

            ApplyShadowNormalOffset(result.position, result.normal);

            #ifdef URHO3D_VERTEX_NEED_TANGENT
                result.tangent = iTangent.xyz;
                result.bitangent = right;
            #endif
        #endif

        return result;
    }
#elif defined(URHO3D_GEOMETRY_TRAIL_BONE)
    VertexTransform GetVertexTransform()
    {
        mat4 modelMatrix = GetModelMatrix();
        half3 right = iTangent.xyz - iPos.xyz;
        half3 front = normalize(iNormal);
        half3 up = normalize(cross(front, right));

        VertexTransform result;
        result.position = vec4((iPos.xyz + right * iTangent.w), 1.0) * modelMatrix;

        #ifdef URHO3D_VERTEX_NEED_NORMAL
            result.normal = up;

            ApplyShadowNormalOffset(result.position, result.normal);

            #ifdef URHO3D_VERTEX_NEED_TANGENT
                result.tangent = front;
                result.bitangent = normalize(cross(result.tangent, result.normal));
            #endif
        #endif

        return result;
    }
#endif

#endif // URHO3D_VERTEX_SHADER

#endif // _VERTEX_TRANSFORM_GLSL_
