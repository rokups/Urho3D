//
// Copyright (c) 2017-2020 the rbfx project.
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

#include "../Graphics/Camera.h"
#include "../Graphics/Drawable.h"
#include "../Graphics/Light.h"
#include "../Graphics/Octree.h"
#include "../Math/Polyhedron.h"
#include "../RenderPipeline/BatchCompositor.h"
#include "../RenderPipeline/DrawableProcessor.h"
#include "../RenderPipeline/LightProcessor.h"
#include "../RenderPipeline/LightProcessorQuery.h"
#include "../RenderPipeline/ShadowMapAllocator.h"
#include "../RenderPipeline/ShadowSplitProcessor.h"

#include <EASTL/sort.h>

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

/// Calculate view size for given min view size and light parameters
Vector2 CalculateViewSize(const Vector2& minViewSize, const FocusParameters& params)
{
    // Quantize size to reduce swimming
    // Convert view scale with non-linear function for adaptive scale
    if (params.nonUniform_)
    {
        const Vector2 adaptiveViewSize = VectorCeil(VectorSqrt(minViewSize / params.quantize_));
        const Vector2 snappedViewSize = adaptiveViewSize * adaptiveViewSize * params.quantize_;
        return VectorMax(snappedViewSize, Vector2::ONE * params.minView_);
    }
    else if (params.focus_)
    {
        const float maxViewSize = ea::max(minViewSize.x_, minViewSize.y_);
        const float adaptiveViewSize = ceilf(sqrtf(maxViewSize / params.quantize_));
        const float snappedViewSize = ea::max(adaptiveViewSize * adaptiveViewSize * params.quantize_, params.minView_);
        return { snappedViewSize, snappedViewSize };
    }
    else
    {
        return minViewSize;
    }
}

}

ShadowSplitProcessor::ShadowSplitProcessor(LightProcessor* owner, unsigned splitIndex)
    : lightProcessor_(owner)
    , light_(lightProcessor_->GetLight())
    , splitIndex_(splitIndex)
    , shadowCameraNode_(MakeShared<Node>(light_->GetContext()))
    , shadowCamera_(shadowCameraNode_->CreateComponent<Camera>())
{
}

ShadowSplitProcessor::~ShadowSplitProcessor()
{
}

void ShadowSplitProcessor::InitializeDirectional(DrawableProcessor* drawableProcessor,
    const FloatRange& splitRange, const ea::vector<Drawable*>& litGeometries)
{
    Camera* cullCamera = drawableProcessor->GetFrameInfo().camera_;
    const FocusParameters& focusParameters = light_->GetShadowFocus();

    // Initialize split Z ranges
    cascadeZRange_ = splitRange;
    focusedCascadeZRange_ = focusParameters.focus_
        ? (drawableProcessor->GetSceneZRange() & cascadeZRange_) : cascadeZRange_;

    // Initialize shadow camera
    InitializeBaseDirectionalCamera(cullCamera);

    const BoundingBox lightSpaceBoundingBox = GetSplitShadowBoundingBoxInLightSpace(drawableProcessor, litGeometries);
    shadowCamera_->SetFarClip(lightSpaceBoundingBox.max_.z_);

    AdjustDirectionalLightCamera(lightSpaceBoundingBox, 0.0f);
}

void ShadowSplitProcessor::InitializeSpot()
{
    Node* lightNode = light_->GetNode();
    shadowCameraNode_->SetTransform(lightNode->GetWorldPosition(), lightNode->GetWorldRotation());
    shadowCamera_->SetNearClip(light_->GetShadowNearFarRatio() * light_->GetRange());
    shadowCamera_->SetFarClip(light_->GetRange());
    shadowCamera_->SetFov(light_->GetFov());
    shadowCamera_->SetAspectRatio(light_->GetAspectRatio());
    shadowCamera_->SetOrthographic(false);
    shadowCamera_->SetZoom(1.0f);
}

void ShadowSplitProcessor::InitializePoint(CubeMapFace face)
{
    static const Vector3* directions[] =
    {
        &Vector3::RIGHT,
        &Vector3::LEFT,
        &Vector3::UP,
        &Vector3::DOWN,
        &Vector3::FORWARD,
        &Vector3::BACK
    };

    // When making a shadowed point light, align the splits along X, Y and Z axes regardless of light rotation
    shadowCameraNode_->SetPosition(light_->GetNode()->GetWorldPosition());
    shadowCameraNode_->SetDirection(*directions[static_cast<unsigned>(face)]);
    shadowCamera_->SetNearClip(light_->GetShadowNearFarRatio() * light_->GetRange());
    shadowCamera_->SetFarClip(light_->GetRange());
    shadowCamera_->SetFov(90.0f);
    shadowCamera_->SetAspectRatio(1.0f);
    shadowCamera_->SetOrthographic(false);
    shadowCamera_->SetZoom(1.0f);
}

void ShadowSplitProcessor::ProcessDirectionalShadowCasters(
    DrawableProcessor* drawableProcessor, ea::vector<Drawable*>& shadowCastersBuffer)
{
    shadowCasters_.clear();
    unsortedShadowBatches_.clear();
    sortedShadowBatches_.clear();

    // Skip split if outside of the scene
    if (!drawableProcessor->GetSceneZRange().Interset(cascadeZRange_))
        return;

    // Query shadow casters
    const FrameInfo& frameInfo = drawableProcessor->GetFrameInfo();
    Camera* cullCamera = frameInfo.camera_;
    Octree* octree = frameInfo.octree_;

    DirectionalLightShadowCasterQuery query(
        shadowCastersBuffer, shadowCamera_->GetFrustum(), DRAWABLE_GEOMETRY, light_, cullCamera->GetViewMask());
    octree->GetDrawables(query);

    // Preprocess shadow casters
    drawableProcessor->PreprocessShadowCasters(shadowCasters_, shadowCastersBuffer, cascadeZRange_, light_, shadowCamera_);
}

void ShadowSplitProcessor::ProcessSpotShadowCasters(
    DrawableProcessor* drawableProcessor, const ea::vector<Drawable*>& shadowCasterCandidates)
{
    shadowCasters_.clear();
    unsortedShadowBatches_.clear();
    sortedShadowBatches_.clear();

    // Preprocess shadow casters
    drawableProcessor->PreprocessShadowCasters(shadowCasters_, shadowCasterCandidates, {}, light_, shadowCamera_);
}

void ShadowSplitProcessor::ProcessPointShadowCasters(
    DrawableProcessor* drawableProcessor, const ea::vector<Drawable*>& shadowCasterCandidates)
{
    shadowCasters_.clear();
    unsortedShadowBatches_.clear();
    sortedShadowBatches_.clear();

    // Check that the face is visible: if not, can skip the split
    Camera* cullCamera = drawableProcessor->GetFrameInfo().camera_;
    const Frustum& cullCameraFrustum = cullCamera->GetFrustum();
    const Frustum& shadowCameraFrustum = shadowCamera_->GetFrustum();

    if (cullCameraFrustum.IsInsideFast(BoundingBox(shadowCameraFrustum)) == OUTSIDE)
        return;

    // Preprocess shadow casters
    drawableProcessor->PreprocessShadowCasters(shadowCasters_, shadowCasterCandidates, {}, light_, shadowCamera_);
}

void ShadowSplitProcessor::FinalizeShadow(const ShadowMapRegion& shadowMap, unsigned pcfKernelSize)
{
    shadowMap_ = shadowMap;

    const auto shadowMapWidth = static_cast<float>(shadowMap_.rect_.Width());
    const LightType lightType = light_->GetLightType();

    if (lightType == LIGHT_DIRECTIONAL)
    {
        BoundingBox shadowBox;
        shadowBox.max_.y_ = shadowCamera_->GetOrthoSize() * 0.5f;
        shadowBox.max_.x_ = shadowCamera_->GetAspectRatio() * shadowBox.max_.y_;
        shadowBox.min_.y_ = -shadowBox.max_.y_;
        shadowBox.min_.x_ = -shadowBox.max_.x_;

        // Requantize and snap to shadow map texels
        AdjustDirectionalLightCamera(shadowBox, shadowMapWidth);
    }

    const unsigned padding = ea::min(4u, 1 + pcfKernelSize / 2);
    const float effectiveShadowMapWidth = shadowMapWidth - 2.0f * padding;
    shadowCamera_->SetZoom(effectiveShadowMapWidth / shadowMapWidth);

    // Estimate shadow map texel size. Exact size for directional light, upper bound for point and spot lights.
    const Vector2 cameraSize = shadowCamera_->GetViewSizeAt(shadowCamera_->GetFarClip());
    shadowMapWorldSpaceTexelSize_ = ea::max(cameraSize.x_, cameraSize.y_) / shadowMapWidth;
}

void ShadowSplitProcessor::InitializeBaseDirectionalCamera(Camera* cullCamera)
{
    Node* lightNode = light_->GetNode();
    const float extrusionDistance = Min(cullCamera->GetFarClip(), light_->GetShadowMaxExtrusion());

    const Vector3 position = cullCamera->GetNode()->GetWorldPosition() - extrusionDistance * lightNode->GetWorldDirection();
    shadowCameraNode_->SetTransform(position, lightNode->GetWorldRotation());

    shadowCamera_->SetOrthographic(true);
    shadowCamera_->SetAspectRatio(1.0f);
    shadowCamera_->SetNearClip(0.0f);
    shadowCamera_->SetZoom(1.0f);
}

BoundingBox ShadowSplitProcessor::GetLitGeometriesBoundingBox(
    DrawableProcessor* drawableProcessor, const ea::vector<Drawable*>& litGeometries) const
{
    BoundingBox litGeometriesBox;
    for (Drawable* drawable : litGeometries)
    {
        const FloatRange& geometryZRange = drawableProcessor->GetGeometryZRange(drawable->GetDrawableIndex());
        if (geometryZRange.Interset(cascadeZRange_))
            litGeometriesBox.Merge(drawable->GetWorldBoundingBox());
    }
    return litGeometriesBox;
}

BoundingBox ShadowSplitProcessor::GetSplitShadowBoundingBoxInLightSpace(
    DrawableProcessor* drawableProcessor, const ea::vector<Drawable*>& litGeometries) const
{
    Camera* cullCamera = drawableProcessor->GetFrameInfo().camera_;
    const FocusParameters& focusParameters = light_->GetShadowFocus();
    const Frustum splitFrustum = cullCamera->GetSplitFrustum(focusedCascadeZRange_.first, focusedCascadeZRange_.second);

    Polyhedron frustumVolume;
    frustumVolume.Define(splitFrustum);

    // Focus frustum volume onto lit geometries
    // If volume became empty, restore it to avoid zero size
    if (focusParameters.focus_)
    {
        const BoundingBox litGeometriesBox = GetLitGeometriesBoundingBox(drawableProcessor, litGeometries);

        if (litGeometriesBox.Defined())
        {
            frustumVolume.Clip(litGeometriesBox);
            if (frustumVolume.Empty())
                frustumVolume.Define(splitFrustum);
        }
    }

    // Transform frustum volume to light space
    const Matrix3x4& lightView = shadowCamera_->GetView();
    frustumVolume.Transform(lightView);

    // Fit the frustum volume inside a bounding box. If uniform size, use a sphere instead
    BoundingBox shadowBox;
    if (!focusParameters.nonUniform_)
        shadowBox.Define(Sphere(frustumVolume));
    else
        shadowBox.Define(frustumVolume);
    return shadowBox;
}

void ShadowSplitProcessor::AdjustDirectionalLightCamera(const BoundingBox& lightSpaceBoundingBox, float shadowMapSize)
{
    const FocusParameters& focusParameters = light_->GetShadowFocus();

    // Evaluate shadow split rectangle in light space
    const Rect lightSpaceRect{ lightSpaceBoundingBox.min_.ToVector2(), lightSpaceBoundingBox.max_.ToVector2() };
    const Vector2 center = lightSpaceRect.Center();
    const Vector2 viewSize = CalculateViewSize(lightSpaceRect.Size(), focusParameters);
    shadowCamera_->SetOrthoSize(viewSize);

    // Center shadow camera to the light space
    const Quaternion lightRotation(shadowCameraNode_->GetWorldRotation());
    shadowCameraNode_->Translate(lightRotation * Vector3(center), TS_WORLD);

    // If the shadow map viewport is known, snap to whole texels
    if (shadowMapSize > 0.0f)
    {
        const Vector3 lightSpacePosition(lightRotation.Inverse() * shadowCameraNode_->GetWorldPosition());
        // Take into account that shadow map border will not be used
        const float invActualSize = 1.0f / (shadowMapSize - 2.0f);
        const Vector2 texelSize(viewSize.x_ * invActualSize, viewSize.y_ * invActualSize);
        const Vector3 snap(-fmodf(lightSpacePosition.x_, texelSize.x_), -fmodf(lightSpacePosition.y_, texelSize.y_), 0.0f);
        shadowCameraNode_->Translate(lightRotation * snap, TS_WORLD);
    }
}

Matrix4 ShadowSplitProcessor::GetWorldToShadowSpaceMatrix(float subPixelOffset) const
{
    if (!shadowMap_)
        return Matrix4::IDENTITY;

    const IntRect& viewport = shadowMap_.rect_;
    const Matrix3x4& shadowView = shadowCamera_->GetView();
    const Matrix4 shadowProj = shadowCamera_->GetGPUProjection();
    const IntVector2 textureSize = shadowMap_.texture_->GetSize();

    Vector3 offset;
    Vector3 scale;

    // Apply viewport offset and scale
    offset.x_ = static_cast<float>(viewport.left_) / textureSize.x_;
    offset.y_ = static_cast<float>(viewport.top_) / textureSize.y_;
    offset.z_ = 0.0f;
    scale.x_ = 0.5f * static_cast<float>(viewport.Width()) / textureSize.x_;
    scale.y_ = 0.5f * static_cast<float>(viewport.Height()) / textureSize.y_;
    scale.z_ = 1.0f;

    offset.x_ += scale.x_;
    offset.y_ += scale.y_;

    // Apply GAPI-specific transforms
    assert(Graphics::GetPixelUVOffset() == Vector2::ZERO);
#ifdef URHO3D_OPENGL
    offset.z_ = 0.5f;
    scale.z_ = 0.5f;
    offset.y_ = 1.0f - offset.y_;
#else
    scale.y_ = -scale.y_;
#endif

    // Apply sub-pixel offset if necessary
    offset.x_ -= subPixelOffset / textureSize.x_;
    offset.y_ -= subPixelOffset / textureSize.y_;

    // Make final matrix
    Matrix4 texAdjust(Matrix4::IDENTITY);
    texAdjust.SetTranslation(offset);
    texAdjust.SetScale(scale);

    return texAdjust * shadowProj * shadowView;
}

void ShadowSplitProcessor::FinalizeShadowBatches()
{
    BatchCompositor::FillSortKeys(sortedShadowBatches_, unsortedShadowBatches_);
    ea::sort(sortedShadowBatches_.begin(), sortedShadowBatches_.end());
    shadowBatches_ = { sortedShadowBatches_,
        BatchRenderFlag::EnableInstancingForStaticGeometry | BatchRenderFlag::DisableColorOutput };
}

}
