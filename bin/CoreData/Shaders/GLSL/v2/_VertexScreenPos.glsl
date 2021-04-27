#ifndef _VERTEX_SCREEN_POS_GLSL_
#define _VERTEX_SCREEN_POS_GLSL_

#ifdef URHO3D_VERTEX_SHADER

mediump mat3 GetCameraRot()
{
    return mat3(cViewInv[0][0], cViewInv[0][1], cViewInv[0][2],
        cViewInv[1][0], cViewInv[1][1], cViewInv[1][2],
        cViewInv[2][0], cViewInv[2][1], cViewInv[2][2]);
}

vec4 GetScreenPos(const vec4 clipPos)
{
    return vec4(
        clipPos.x * cGBufferOffsets.z + cGBufferOffsets.x * clipPos.w,
#ifdef URHO3D_FEATURE_FRAMEBUFFER_Y_INVERTED
        -clipPos.y * cGBufferOffsets.w + cGBufferOffsets.y * clipPos.w,
#else
        clipPos.y * cGBufferOffsets.w + cGBufferOffsets.y * clipPos.w,
#endif
        0.0,
        clipPos.w);
}

vec2 GetScreenPosPreDiv(const vec4 clipPos)
{
    return vec2(
        clipPos.x / clipPos.w * cGBufferOffsets.z + cGBufferOffsets.x,
#ifdef URHO3D_FEATURE_FRAMEBUFFER_Y_INVERTED
        -clipPos.y / clipPos.w * cGBufferOffsets.w + cGBufferOffsets.y);
#else
        clipPos.y / clipPos.w * cGBufferOffsets.w + cGBufferOffsets.y);
#endif
}

vec2 GetQuadTexCoord(const vec4 clipPos)
{
    return vec2(
        clipPos.x / clipPos.w * 0.5 + 0.5,
#ifdef URHO3D_FEATURE_FRAMEBUFFER_Y_INVERTED
        -clipPos.y / clipPos.w * 0.5 + 0.5);
#else
        clipPos.y / clipPos.w * 0.5 + 0.5);
#endif
}

vec2 GetQuadTexCoordNoFlip(const vec3 worldPos)
{
    return vec2(
        worldPos.x * 0.5 + 0.5,
        -worldPos.y * 0.5 + 0.5);
}

vec3 GetFarRay(const vec4 clipPos)
{
    vec3 viewRay = vec3(
        clipPos.x / clipPos.w * cFrustumSize.x,
        clipPos.y / clipPos.w * cFrustumSize.y,
        cFrustumSize.z);

    return viewRay * GetCameraRot();
}

vec3 GetNearRay(const vec4 clipPos)
{
    vec3 viewRay = vec3(
        clipPos.x / clipPos.w * cFrustumSize.x,
        clipPos.y / clipPos.w * cFrustumSize.y,
        0.0);

    return (viewRay * GetCameraRot()) * cDepthMode.x;
}

#endif // URHO3D_VERTEX_SHADER
#endif // _VERTEX_SCREEN_POS_GLSL_
