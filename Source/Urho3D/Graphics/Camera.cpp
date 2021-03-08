//
// Copyright (c) 2008-2020 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "../Precompiled.h"

#include "../Core/Context.h"
#include "../Graphics/Camera.h"
#include "../Graphics/DebugRenderer.h"
#include "../Graphics/Drawable.h"
#include "../Graphics/Zone.h"
#include "../Scene/Node.h"

#include "../DebugNew.h"

namespace Urho3D
{

extern const char* SCENE_CATEGORY;

static const char* fillModeNames[] =
{
    "Solid",
    "Wireframe",
    "Point",
    nullptr
};

static const Matrix4 flipMatrix(
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, -1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
);

Camera::Camera(Context* context) :
    Component(context),
    orthographic_(false),
    nearClip_(DEFAULT_NEARCLIP),
    farClip_(DEFAULT_FARCLIP),
    fov_(DEFAULT_CAMERA_FOV),
    orthoSize_(DEFAULT_ORTHOSIZE),
    aspectRatio_(1.0f),
    zoom_(1.0f),
    lodBias_(1.0f),
    viewMask_(DEFAULT_VIEWMASK),
    zoneMask_(DEFAULT_ZONEMASK),
    viewOverrideFlags_(VO_NONE),
    fillMode_(FILL_SOLID),
    projectionOffset_(Vector2::ZERO),
    reflectionPlane_(Plane::UP),
    clipPlane_(Plane::UP),
    autoAspectRatio_(true),
    flipVertical_(false),
    useReflection_(false),
    useClipping_(false)
{
    reflectionMatrix_ = reflectionPlane_.ReflectionMatrix();
}

Camera::~Camera() = default;

void Camera::RegisterObject(Context* context)
{
    context->RegisterFactory<Camera>(SCENE_CATEGORY);

    URHO3D_ACCESSOR_ATTRIBUTE("Is Enabled", IsEnabled, SetEnabled, bool, true, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Near Clip", GetNearClip, SetNearClip, float, DEFAULT_NEARCLIP, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Far Clip", GetFarClip, SetFarClip, float, DEFAULT_FARCLIP, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("FOV", GetFov, SetFov, float, DEFAULT_CAMERA_FOV, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Aspect Ratio", GetAspectRatio, SetAspectRatioInternal, float, 1.0f, AM_DEFAULT);
    URHO3D_ENUM_ATTRIBUTE("Fill Mode", fillMode_, fillModeNames, FILL_SOLID, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Auto Aspect Ratio", bool, autoAspectRatio_, true, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Orthographic", IsOrthographic, SetOrthographic, bool, false, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Orthographic Size", GetOrthoSize, SetOrthoSizeAttr, float, DEFAULT_ORTHOSIZE, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Zoom", GetZoom, SetZoom, float, 1.0f, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("LOD Bias", GetLodBias, SetLodBias, float, 1.0f, AM_DEFAULT);
    URHO3D_ATTRIBUTE("View Mask", int, viewMask_, DEFAULT_VIEWMASK, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Zone Mask", int, zoneMask_, DEFAULT_ZONEMASK, AM_DEFAULT);
    URHO3D_ATTRIBUTE("View Override Flags", unsigned, viewOverrideFlags_.AsInteger(), VO_NONE, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Projection Offset", GetProjectionOffset, SetProjectionOffset, Vector2, Vector2::ZERO, AM_DEFAULT);
    URHO3D_MIXED_ACCESSOR_ATTRIBUTE("Reflection Plane", GetReflectionPlaneAttr, SetReflectionPlaneAttr, Vector4,
        Vector4(0.0f, 1.0f, 0.0f, 0.0f), AM_DEFAULT);
    URHO3D_MIXED_ACCESSOR_ATTRIBUTE("Clip Plane", GetClipPlaneAttr, SetClipPlaneAttr, Vector4, Vector4(0.0f, 1.0f, 0.0f, 0.0f),
        AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Use Reflection", GetUseReflection, SetUseReflection, bool, false, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Use Clipping", GetUseClipping, SetUseClipping, bool, false, AM_DEFAULT);
}

void Camera::DrawDebugGeometry(DebugRenderer* debug, bool depthTest)
{
    debug->AddFrustum(GetFrustum(), Color::WHITE, depthTest);
}

void Camera::SetNearClip(float nearClip)
{
    nearClip_ = Max(nearClip, M_MIN_NEARCLIP);
    cachedFrustum_.Invalidate();
    cachedProjection_.Invalidate();
    cachedViewProj_.Invalidate();
    MarkNetworkUpdate();
}

void Camera::SetFarClip(float farClip)
{
    farClip_ = Max(farClip, M_MIN_NEARCLIP);
    cachedFrustum_.Invalidate();
    cachedProjection_.Invalidate();
    cachedViewProj_.Invalidate();
    MarkNetworkUpdate();
}

void Camera::SetFov(float fov)
{
    fov_ = Clamp(fov, 0.0f, M_MAX_FOV);
    cachedFrustum_.Invalidate();
    cachedProjection_.Invalidate();
    cachedViewProj_.Invalidate();
    MarkNetworkUpdate();
}

void Camera::SetOrthoSize(float orthoSize)
{
    orthoSize_ = orthoSize;
    aspectRatio_ = 1.0f;
    cachedFrustum_.Invalidate();
    cachedProjection_.Invalidate();
    cachedViewProj_.Invalidate();
    MarkNetworkUpdate();
}

void Camera::SetOrthoSize(const Vector2& orthoSize)
{
    autoAspectRatio_ = false;
    orthoSize_ = orthoSize.y_;
    aspectRatio_ = orthoSize.x_ / orthoSize.y_;
    cachedFrustum_.Invalidate();
    cachedProjection_.Invalidate();
    cachedViewProj_.Invalidate();
    MarkNetworkUpdate();
}

void Camera::SetAspectRatio(float aspectRatio)
{
    autoAspectRatio_ = false;
    SetAspectRatioInternal(aspectRatio);
}

void Camera::SetZoom(float zoom)
{
    zoom_ = Max(zoom, M_EPSILON);
    cachedFrustum_.Invalidate();
    cachedProjection_.Invalidate();
    cachedViewProj_.Invalidate();
    MarkNetworkUpdate();
}

void Camera::SetLodBias(float bias)
{
    lodBias_ = Max(bias, M_EPSILON);
    MarkNetworkUpdate();
}

void Camera::SetViewMask(unsigned mask)
{
    viewMask_ = mask;
    MarkNetworkUpdate();
}

void Camera::SetZoneMask(unsigned mask)
{
    zoneMask_ = mask;
    MarkNetworkUpdate();
}

void Camera::SetViewOverrideFlags(ViewOverrideFlags flags)
{
    viewOverrideFlags_ = flags;
    MarkNetworkUpdate();
}

void Camera::SetFillMode(FillMode mode)
{
    fillMode_ = mode;
    MarkNetworkUpdate();
}

void Camera::SetOrthographic(bool enable)
{
    orthographic_ = enable;
    cachedFrustum_.Invalidate();
    cachedProjection_.Invalidate();
    cachedViewProj_.Invalidate();
    MarkNetworkUpdate();
}

void Camera::SetAutoAspectRatio(bool enable)
{
    autoAspectRatio_ = enable;
    MarkNetworkUpdate();
}

void Camera::SetProjectionOffset(const Vector2& offset)
{
    projectionOffset_ = offset;
    cachedProjection_.Invalidate();
    cachedViewProj_.Invalidate();
    MarkNetworkUpdate();
}

void Camera::SetUseReflection(bool enable)
{
    useReflection_ = enable;
    cachedView_.Invalidate();
    cachedViewProj_.Invalidate();
    cachedFrustum_.Invalidate();
    MarkNetworkUpdate();
}

void Camera::SetReflectionPlane(const Plane& plane)
{
    reflectionPlane_ = plane;
    reflectionMatrix_ = reflectionPlane_.ReflectionMatrix();
    cachedView_.Invalidate();
    cachedViewProj_.Invalidate();
    cachedFrustum_.Invalidate();
    MarkNetworkUpdate();
}

void Camera::SetUseClipping(bool enable)
{
    useClipping_ = enable;
    cachedProjection_.Invalidate();
    cachedViewProj_.Invalidate();
    MarkNetworkUpdate();
}

void Camera::SetClipPlane(const Plane& plane)
{
    clipPlane_ = plane;
    MarkNetworkUpdate();
}

void Camera::SetFlipVertical(bool enable)
{
    flipVertical_ = enable;
    cachedViewProj_.Invalidate();
    MarkNetworkUpdate();
}

void Camera::SetProjection(const Matrix4& projection)
{
    const Matrix4 projInverse = projection.Inverse();

    // Calculate the actual near & far clip from the custom matrix
    const float projNearClip = (projInverse * Vector3(0.0f, 0.0f, 0.0f)).z_;
    const float projFarClip = (projInverse * Vector3(0.0f, 0.0f, 1.0f)).z_;
    cachedProjection_.Restore({ projection, projNearClip, projFarClip, true });
    autoAspectRatio_ = false;
    cachedViewProj_.Invalidate();
    cachedFrustum_.Invalidate();
    // Called due to autoAspectRatio changing state, the projection itself is not serialized
    MarkNetworkUpdate();
}

float Camera::GetNearClip() const
{
    if (cachedProjection_.IsInvalidated())
        UpdateProjection();

    return cachedProjection_.Get().projNearClip_;
}

float Camera::GetFarClip() const
{
    if (cachedProjection_.IsInvalidated())
        UpdateProjection();

    return cachedProjection_.Get().projFarClip_;
}

const Frustum& Camera::GetFrustum() const
{
    // Use projection_ instead of GetProjection() so that Y-flip has no effect. Update first if necessary
    if (cachedProjection_.IsInvalidated())
        UpdateProjection();

    const CachedProjection& cache = cachedProjection_.Get();
    if (cachedFrustum_.IsInvalidated())
    {
        Frustum frustum;
        if (cache.customProjection_)
            frustum.Define(cache.projection_ * GetView());
        else
        {
            // If not using a custom projection, prefer calculating frustum from projection parameters instead of matrix
            // for better accuracy
            if (!orthographic_)
                frustum.Define(fov_, aspectRatio_, zoom_, GetNearClip(), GetFarClip(), GetEffectiveWorldTransform());
            else
                frustum.DefineOrtho(orthoSize_, aspectRatio_, zoom_, GetNearClip(), GetFarClip(), GetEffectiveWorldTransform());
        }

        cachedFrustum_.Restore(frustum);
    }

    return cachedFrustum_.Get();
}

Frustum Camera::GetSplitFrustum(float nearClip, float farClip) const
{
    if (cachedProjection_.IsInvalidated())
        UpdateProjection();

    const CachedProjection& cache = cachedProjection_.Get();
    nearClip = Max(nearClip, cache.projNearClip_);
    farClip = Min(farClip, cache.projFarClip_);
    if (farClip < nearClip)
        farClip = nearClip;

    Frustum ret;

    if (cache.customProjection_)
    {
        // DefineSplit() needs to project the near & far distances, so can not use a combined view-projection matrix.
        // Transform to world space afterward instead
        ret.DefineSplit(cache.projection_, nearClip, farClip);
        ret.Transform(GetEffectiveWorldTransform());
    }
    else
    {
        if (!orthographic_)
            ret.Define(fov_, aspectRatio_, zoom_, nearClip, farClip, GetEffectiveWorldTransform());
        else
            ret.DefineOrtho(orthoSize_, aspectRatio_, zoom_, nearClip, farClip, GetEffectiveWorldTransform());
    }

    return ret;
}

Frustum Camera::GetViewSpaceFrustum() const
{
    if (cachedProjection_.IsInvalidated())
        UpdateProjection();

    const CachedProjection& cache = cachedProjection_.Get();

    Frustum ret;

    if (cache.customProjection_)
        ret.Define(cache.projection_);
    else
    {
        if (!orthographic_)
            ret.Define(fov_, aspectRatio_, zoom_, GetNearClip(), GetFarClip());
        else
            ret.DefineOrtho(orthoSize_, aspectRatio_, zoom_, GetNearClip(), GetFarClip());
    }

    return ret;
}

Frustum Camera::GetViewSpaceSplitFrustum(float nearClip, float farClip) const
{
    if (cachedProjection_.IsInvalidated())
        UpdateProjection();

    const CachedProjection& cache = cachedProjection_.Get();

    nearClip = Max(nearClip, cache.projNearClip_);
    farClip = Min(farClip, cache.projFarClip_);
    if (farClip < nearClip)
        farClip = nearClip;

    Frustum ret;

    if (cache.customProjection_)
        ret.DefineSplit(cache.projection_, nearClip, farClip);
    else
    {
        if (!orthographic_)
            ret.Define(fov_, aspectRatio_, zoom_, nearClip, farClip);
        else
            ret.DefineOrtho(orthoSize_, aspectRatio_, zoom_, nearClip, farClip);
    }

    return ret;
}

Ray Camera::GetScreenRay(float x, float y) const
{
    Ray ret;

    // If projection is invalid, just return a ray pointing forward
    if (!IsProjectionValid())
    {
        ret.origin_ = node_ ? node_->GetWorldPosition() : Vector3::ZERO;
        ret.direction_ = node_ ? node_->GetWorldDirection() : Vector3::FORWARD;
        return ret;
    }

    Matrix4 viewProjInverse = (GetProjection() * GetView()).Inverse();

    // The parameters range from 0.0 to 1.0. Expand to normalized device coordinates (-1.0 to 1.0) & flip Y axis
    x = 2.0f * x - 1.0f;
    y = 1.0f - 2.0f * y;
    Vector3 nearPlane(x, y, 0.0f);
    Vector3 farPlane(x, y, 1.0f);

    ret.origin_ = viewProjInverse * nearPlane;
    ret.direction_ = ((viewProjInverse * farPlane) - ret.origin_).Normalized();
    return ret;
}

Vector2 Camera::WorldToScreenPoint(const Vector3& worldPos) const
{
    Vector3 eyeSpacePos = GetView() * worldPos;
    Vector2 ret;

    if (eyeSpacePos.z_ > 0.0f)
    {
        Vector3 screenSpacePos = GetProjection() * eyeSpacePos;
        ret.x_ = screenSpacePos.x_;
        ret.y_ = screenSpacePos.y_;
    }
    else
    {
        ret.x_ = (-eyeSpacePos.x_ > 0.0f) ? -1.0f : 1.0f;
        ret.y_ = (-eyeSpacePos.y_ > 0.0f) ? -1.0f : 1.0f;
    }

    ret.x_ = (ret.x_ / 2.0f) + 0.5f;
    ret.y_ = 1.0f - ((ret.y_ / 2.0f) + 0.5f);
    return ret;
}

Vector3 Camera::ScreenToWorldPoint(const Vector3& screenPos) const
{
    Ray ray = GetScreenRay(screenPos.x_, screenPos.y_);
    Vector3 viewSpaceDir = (GetView() * Vector4(ray.direction_, 0.0f));
    float rayDistance = (Max(screenPos.z_ - GetNearClip(), 0.0f) / viewSpaceDir.z_);
    return ray.origin_ + ray.direction_ * rayDistance;
}

Matrix4 Camera::GetProjection() const
{
    if (cachedProjection_.IsInvalidated())
        UpdateProjection();

    const Matrix4& projection = cachedProjection_.Get().projection_;
    return flipVertical_ ? flipMatrix * projection : projection;
}

Matrix4 Camera::GetGPUProjection() const
{
#ifndef URHO3D_OPENGL
    return GetProjection(); // Already matches API-specific format
#else
    // See formulation for depth range conversion at http://www.ogre3d.org/forums/viewtopic.php?f=4&t=13357
    Matrix4 ret = GetProjection();

    ret.m20_ = 2.0f * ret.m20_ - ret.m30_;
    ret.m21_ = 2.0f * ret.m21_ - ret.m31_;
    ret.m22_ = 2.0f * ret.m22_ - ret.m32_;
    ret.m23_ = 2.0f * ret.m23_ - ret.m33_;

    return ret;
#endif
}

Matrix4 Camera::GetEffectiveGPUViewProjection(float constantDepthBias) const
{
    Matrix4 projection = GetGPUProjection();
    // glPolygonOffset is not supported in GL ES 2.0
#ifdef URHO3D_OPENGL
    const float constantBias = 2.0f * constantDepthBias;
    projection.m22_ += projection.m32_ * constantBias;
    projection.m23_ += projection.m33_ * constantBias;
#endif
    return projection * GetView();
}

void Camera::GetFrustumSize(Vector3& nearSize, Vector3& farSize) const
{
    Frustum viewSpaceFrustum = GetViewSpaceFrustum();
    nearSize = viewSpaceFrustum.vertices_[0];
    farSize = viewSpaceFrustum.vertices_[4];

    /// \todo Necessary? Explain this
    if (flipVertical_)
    {
        nearSize.y_ = -nearSize.y_;
        farSize.y_ = -farSize.y_;
    }
}

float Camera::GetHalfViewSize() const
{
    if (!orthographic_)
        return tanf(fov_ * M_DEGTORAD * 0.5f) / zoom_;
    else
        return orthoSize_ * 0.5f / zoom_;
}

Vector2 Camera::GetViewSizeAt(float z) const
{
    const float halfHeight = GetHalfViewSize();
    const Vector2 halfSize{ aspectRatio_ * halfHeight, halfHeight };
    if (orthographic_)
        return halfSize;
    else
        return halfSize * z;
}

float Camera::GetDistance(const Vector3& worldPos) const
{
    if (!orthographic_)
    {
        const Vector3& cameraPos = node_ ? node_->GetWorldPosition() : Vector3::ZERO;
        return (worldPos - cameraPos).Length();
    }
    else
        return Abs((GetView() * worldPos).z_);
}

float Camera::GetDistanceSquared(const Vector3& worldPos) const
{
    if (!orthographic_)
    {
        const Vector3& cameraPos = node_ ? node_->GetWorldPosition() : Vector3::ZERO;
        return (worldPos - cameraPos).LengthSquared();
    }
    else
    {
        float distance = (GetView() * worldPos).z_;
        return distance * distance;
    }
}

float Camera::GetLodDistance(float distance, float scale, float bias) const
{
    float d = Max(lodBias_ * bias * scale * zoom_, M_EPSILON);
    if (!orthographic_)
        return distance / d;
    else
        return orthoSize_ / d;
}

Quaternion Camera::GetFaceCameraRotation(const Vector3& position, const Quaternion& rotation, FaceCameraMode mode, float minAngle)
{
    if (!node_)
        return rotation;

    switch (mode)
    {
    case FC_ROTATE_XYZ:
        return node_->GetWorldRotation();

    case FC_ROTATE_Y:
        {
            Vector3 euler = rotation.EulerAngles();
            euler.y_ = node_->GetWorldRotation().EulerAngles().y_;
            return Quaternion(euler.x_, euler.y_, euler.z_);
        }

    case FC_LOOKAT_XYZ:
        {
            Quaternion lookAt;
            lookAt.FromLookRotation(position - node_->GetWorldPosition());
            return lookAt;
        }

    case FC_LOOKAT_Y:
    case FC_LOOKAT_MIXED:
        {
            // Mixed mode needs true look-at vector
            const Vector3 lookAtVec(position - node_->GetWorldPosition());
            // While Y-only lookat happens on an XZ plane to make sure there are no unwanted transitions or singularities
            const Vector3 lookAtVecXZ(lookAtVec.x_, 0.0f, lookAtVec.z_);

            Quaternion lookAt;
            lookAt.FromLookRotation(lookAtVecXZ);

            Vector3 euler = rotation.EulerAngles();
            if (mode == FC_LOOKAT_MIXED)
            {
                const float angle = lookAtVec.Angle(rotation * Vector3::UP);
                if (angle > 180 - minAngle)
                    euler.x_ += minAngle - (180 - angle);
                else if (angle < minAngle)
                    euler.x_ -= minAngle - angle;
            }
            euler.y_ = lookAt.EulerAngles().y_;
            return Quaternion(euler.x_, euler.y_, euler.z_);
        }

    default:
        return rotation;
    }
}

Matrix3x4 Camera::GetEffectiveWorldTransform() const
{
    Matrix3x4 worldTransform = node_ ? Matrix3x4(node_->GetWorldPosition(), node_->GetWorldRotation(), 1.0f) : Matrix3x4::IDENTITY;
    return useReflection_ ? reflectionMatrix_ * worldTransform : worldTransform;
}

bool Camera::IsProjectionValid() const
{
    return GetFarClip() > GetNearClip();
}

const Matrix3x4& Camera::GetView() const
{
    if (cachedView_.IsInvalidated())
    {
        // Note: view matrix is unaffected by node or parent scale
        const Matrix3x4 view = GetEffectiveWorldTransform().Inverse();
        cachedView_.Restore(view);
    }

    return cachedView_.Get();
}

const Matrix4& Camera::GetViewProj() const
{
    if (cachedViewProj_.IsInvalidated())
        UpdateViewProjectionMatrices();
    return cachedViewProj_.Get().viewProj_;
}

const Matrix4& Camera::GetInverseViewProj() const
{
    if (cachedViewProj_.IsInvalidated())
        UpdateViewProjectionMatrices();
    return cachedViewProj_.Get().inverseViewProj_;
}

void Camera::SetAspectRatioInternal(float aspectRatio)
{
    if (aspectRatio != aspectRatio_)
    {
        aspectRatio_ = aspectRatio;
        cachedFrustum_.Invalidate();
        cachedProjection_.Invalidate();
        cachedViewProj_.Invalidate();
    }
    MarkNetworkUpdate();
}

void Camera::SetOrthoSizeAttr(float orthoSize)
{
    orthoSize_ = orthoSize;
    cachedFrustum_.Invalidate();
    cachedProjection_.Invalidate();
    cachedViewProj_.Invalidate();
    MarkNetworkUpdate();
}

void Camera::SetReflectionPlaneAttr(const Vector4& value)
{
    SetReflectionPlane(Plane(value));
}

void Camera::SetClipPlaneAttr(const Vector4& value)
{
    SetClipPlane(Plane(value));
}

Vector4 Camera::GetReflectionPlaneAttr() const
{
    return reflectionPlane_.ToVector4();
}

Vector4 Camera::GetClipPlaneAttr() const
{
    return clipPlane_.ToVector4();
}

void Camera::OnNodeSet(Node* node)
{
    if (node)
        node->AddListener(this);
}

void Camera::OnMarkedDirty(Node* node)
{
    cachedFrustum_.Invalidate();
    cachedView_.Invalidate();
    cachedViewProj_.Invalidate();
}

void Camera::UpdateProjection() const
{
    // Start from a zero matrix in case it was custom previously
    Matrix4 projection = Matrix4::ZERO;
    float projNearClip{};
    float projFarClip{};

    if (!orthographic_)
    {
        float h = (1.0f / tanf(fov_ * M_DEGTORAD * 0.5f)) * zoom_;
        float w = h / aspectRatio_;
        float q = farClip_ / (farClip_ - nearClip_);
        float r = -q * nearClip_;

        projection.m00_ = w;
        projection.m02_ = projectionOffset_.x_ * 2.0f;
        projection.m11_ = h;
        projection.m12_ = projectionOffset_.y_ * 2.0f;
        projection.m22_ = q;
        projection.m23_ = r;
        projection.m32_ = 1.0f;
        projNearClip = nearClip_;
        projFarClip = farClip_;
    }
    else
    {
        float h = (1.0f / (orthoSize_ * 0.5f)) * zoom_;
        float w = h / aspectRatio_;
        float q = 1.0f / farClip_;
        float r = 0.0f;

        projection.m00_ = w;
        projection.m03_ = projectionOffset_.x_ * 2.0f;
        projection.m11_ = h;
        projection.m13_ = projectionOffset_.y_ * 2.0f;
        projection.m22_ = q;
        projection.m23_ = r;
        projection.m33_ = 1.0f;
        // Near clip does not affect depth accuracy in ortho projection, so let it stay 0 to avoid problems with shader depth parameters
        projNearClip = 0.0f;
        projFarClip = farClip_;
    }

    cachedProjection_.Restore({ projection, projNearClip, projFarClip, false });
}

void Camera::UpdateViewProjectionMatrices() const
{
    const Matrix4 viewProj = GetProjection() * GetView();
    const Matrix4 inverseViewProj = viewProj.Inverse();
    cachedViewProj_.Restore({ viewProj, inverseViewProj });
}

const Color& Camera::GetEffectiveAmbientColor() const
{
    return zone_ ? zone_->GetAmbientColor() : Color::TRANSPARENT_BLACK;
}

float Camera::GetEffectiveAmbientBrightness() const
{
    return zone_ ? zone_->GetAmbientBrightness() : 1.0f;
}

const Color& Camera::GetEffectiveFogColor() const
{
    return zone_ ? zone_->GetFogColor() : Color::TRANSPARENT_BLACK;
}

float Camera::GetEffectiveFogStart() const
{
    return zone_ ? zone_->GetFogStart() : M_LARGE_VALUE;
}

float Camera::GetEffectiveFogEnd() const
{
    return zone_ ? zone_->GetFogEnd() : M_LARGE_VALUE;
}
}
