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

#include "../Core/Context.h"
#include "../Core/IteratorRange.h"
#include "../Core/WorkQueue.h"
#include "../Math/Polyhedron.h"
#include "../Graphics/Camera.h"
#include "../Graphics/SceneLight.h"
#include "../Graphics/Octree.h"
#include "../Graphics/OctreeQuery.h"
#include "../Graphics/Renderer.h"
#include "../Scene/Node.h"

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

/// Frustum Query for point light.
struct PointLightLitGeometriesQuery : public SphereOctreeQuery
{
    /// Return light sphere for the query.
    static Sphere GetLightSphere(Light* light)
    {
        return Sphere(light->GetNode()->GetWorldPosition(), light->GetRange());
    }

    /// Construct.
    PointLightLitGeometriesQuery(ea::vector<Drawable*>& result, ea::vector<Drawable*>* shadowCasters,
        const SceneDrawableData& transientData, Light* light, unsigned viewMask)
        : SphereOctreeQuery(result, GetLightSphere(light), DRAWABLE_GEOMETRY, viewMask)
        , shadowCasters_(shadowCasters)
        , transientData_(&transientData)
        , lightMask_(light->GetLightMaskEffective())
    {
        if (shadowCasters_)
            shadowCasters_->clear();
    }

    void TestDrawables(Drawable** start, Drawable** end, bool inside) override
    {
        for (Drawable* drawable : MakeIteratorRange(start, end))
        {
            const auto isLitOrShadowCaster = IsLitOrShadowCaster(drawable, inside);
            if (isLitOrShadowCaster.first)
                result_.push_back(drawable);
            if (isLitOrShadowCaster.second)
                shadowCasters_->push_back(drawable);
        }
    }

    /// Return whether the drawable is lit and/or shadow caster.
    ea::pair<bool, bool> IsLitOrShadowCaster(Drawable* drawable, bool inside) const
    {
        const unsigned drawableIndex = drawable->GetDrawableIndex();
        const unsigned traits = transientData_->traits_[drawableIndex];

        const bool isInside = (drawable->GetDrawableFlags() & drawableFlags_)
            && (drawable->GetViewMask() & viewMask_)
            && (inside || sphere_.IsInsideFast(drawable->GetWorldBoundingBox()));
        const bool isLit = isInside
            && (traits & SceneDrawableData::DrawableVisibleGeometry)
            && (drawable->GetLightMask() & lightMask_);
        const bool isShadowCaster = shadowCasters_ && isInside
            && drawable->GetCastShadows()
            && (drawable->GetShadowMask() & lightMask_);
        return { isLit, isShadowCaster };
    }

    /// Result array of shadow casters, if applicable.
    ea::vector<Drawable*>* shadowCasters_{};
    /// Visiblity cache.
    const SceneDrawableData* transientData_{};
    /// Light mask to check.
    unsigned lightMask_{};
};

/// Frustum Query for spot light.
struct SpotLightLitGeometriesQuery : public FrustumOctreeQuery
{
    /// Construct.
    SpotLightLitGeometriesQuery(ea::vector<Drawable*>& result, ea::vector<Drawable*>* shadowCasters,
        const SceneDrawableData& transientData, Light* light, unsigned viewMask)
        : FrustumOctreeQuery(result, light->GetFrustum(), DRAWABLE_GEOMETRY, viewMask)
        , shadowCasters_(shadowCasters)
        , transientData_(&transientData)
        , lightMask_(light->GetLightMaskEffective())
    {
        if (shadowCasters_)
            shadowCasters_->clear();
    }

    void TestDrawables(Drawable** start, Drawable** end, bool inside) override
    {
        for (Drawable* drawable : MakeIteratorRange(start, end))
        {
            const auto isLitOrShadowCaster = IsLitOrShadowCaster(drawable, inside);
            if (isLitOrShadowCaster.first)
                result_.push_back(drawable);
            if (isLitOrShadowCaster.second)
                shadowCasters_->push_back(drawable);
        }
    }

    /// Return whether the drawable is lit and/or shadow caster.
    ea::pair<bool, bool> IsLitOrShadowCaster(Drawable* drawable, bool inside) const
    {
        const unsigned drawableIndex = drawable->GetDrawableIndex();
        const unsigned traits = transientData_->traits_[drawableIndex];

        const bool isInside = (drawable->GetDrawableFlags() & drawableFlags_)
            && (drawable->GetViewMask() & viewMask_)
            && (inside || frustum_.IsInsideFast(drawable->GetWorldBoundingBox()));
        const bool isLit = isInside
            && (traits & SceneDrawableData::DrawableVisibleGeometry)
            && (drawable->GetLightMask() & lightMask_);
        const bool isShadowCaster = shadowCasters_ && isInside
            && drawable->GetCastShadows()
            && (drawable->GetShadowMask() & lightMask_);
        return { isLit, isShadowCaster };
    }

    /// Result array of shadow casters, if applicable.
    ea::vector<Drawable*>* shadowCasters_{};
    /// Visiblity cache.
    const SceneDrawableData* transientData_{};
    /// Light mask to check.
    unsigned lightMask_{};
};

/// %Frustum octree query for directional light shadowcasters.
class DirectionalLightShadowCasterOctreeQuery : public FrustumOctreeQuery
{
public:
    /// Construct with frustum and query parameters.
    DirectionalLightShadowCasterOctreeQuery(ea::vector<Drawable*>& result,
        const Frustum& frustum, DrawableFlags drawableFlags, Light* light, unsigned viewMask)
        : FrustumOctreeQuery(result, frustum, drawableFlags, viewMask)
        , lightMask_(light->GetLightMask())
    {
    }

    /// Intersection test for drawables.
    void TestDrawables(Drawable** start, Drawable** end, bool inside) override
    {
        for (Drawable* drawable : MakeIteratorRange(start, end))
        {
            if (IsShadowCaster(drawable, inside))
                result_.push_back(drawable);
        }
    }

    /// Return whether the drawable is shadow caster.
    bool IsShadowCaster(Drawable* drawable, bool inside) const
    {
        return drawable->GetCastShadows()
            && (drawable->GetDrawableFlags() & drawableFlags_)
            && (drawable->GetViewMask() & viewMask_)
            && (drawable->GetShadowMask() & lightMask_)
            && (inside || frustum_.IsInsideFast(drawable->GetWorldBoundingBox()));
    }

    /// Light mask to check.
    unsigned lightMask_{};
};

}

void SceneLightShadowSplit::SetupDirLightShadowCamera(Light* light, Camera* cullCamera,
    const ea::vector<Drawable*>& litGeometries,
    const DrawableZRange& sceneZRange, const ea::vector<DrawableZRange>& drawableZRanges)
{
    Node* shadowCameraNode = shadowCamera_->GetNode();
    Node* lightNode = light->GetNode();
    float extrusionDistance = Min(cullCamera->GetFarClip(), light->GetShadowMaxExtrusion());
    const FocusParameters& parameters = light->GetShadowFocus();

    // Calculate initial position & rotation
    Vector3 pos = cullCamera->GetNode()->GetWorldPosition() - extrusionDistance * lightNode->GetWorldDirection();
    shadowCameraNode->SetTransform(pos, lightNode->GetWorldRotation());

    // Use the scene Z bounds to limit frustum size if applicable
    const DrawableZRange splitZRange = parameters.focus_ ? sceneZRange & zRange_ : zRange_;

    // Calculate main camera shadowed frustum in light's view space
    Frustum splitFrustum = cullCamera->GetSplitFrustum(splitZRange.first, splitZRange.second);
    Polyhedron frustumVolume;
    frustumVolume.Define(splitFrustum);
    // If focusing enabled, clip the frustum volume by the combined bounding box of the lit geometries within the frustum
    if (parameters.focus_)
    {
        BoundingBox litGeometriesBox;
        unsigned lightMask = light->GetLightMaskEffective();

        for (Drawable* drawable : litGeometries)
        {
            const DrawableZRange& drawableZRange = drawableZRanges[drawable->GetDrawableIndex()];
            if (drawableZRange.Interset(splitZRange))
                litGeometriesBox.Merge(drawable->GetWorldBoundingBox());
        }

        if (litGeometriesBox.Defined())
        {
            frustumVolume.Clip(litGeometriesBox);
            // If volume became empty, restore it to avoid zero size
            if (frustumVolume.Empty())
                frustumVolume.Define(splitFrustum);
        }
    }

    // Transform frustum volume to light space
    const Matrix3x4& lightView = shadowCamera_->GetView();
    frustumVolume.Transform(lightView);

    // Fit the frustum volume inside a bounding box. If uniform size, use a sphere instead
    BoundingBox shadowBox;
    if (!parameters.nonUniform_)
        shadowBox.Define(Sphere(frustumVolume));
    else
        shadowBox.Define(frustumVolume);

    shadowCamera_->SetOrthographic(true);
    shadowCamera_->SetAspectRatio(1.0f);
    shadowCamera_->SetNearClip(0.0f);
    shadowCamera_->SetFarClip(shadowBox.max_.z_);

    // Center shadow camera on the bounding box. Can not snap to texels yet as the shadow map viewport is unknown
    shadowMap_.region_ = IntRect::ZERO;
    QuantizeDirLightShadowCamera(parameters, shadowBox);
}

void SceneLightShadowSplit::QuantizeDirLightShadowCamera(const FocusParameters& parameters, const BoundingBox& viewBox)
{
    Node* shadowCameraNode = shadowCamera_->GetNode();
    const auto shadowMapWidth = static_cast<float>(shadowMap_.region_.Width());

    const float minX = viewBox.min_.x_;
    const float minY = viewBox.min_.y_;
    const float maxX = viewBox.max_.x_;
    const float maxY = viewBox.max_.y_;

    const Vector2 center((minX + maxX) * 0.5f, (minY + maxY) * 0.5f);
    Vector2 viewSize(maxX - minX, maxY - minY);

    // Quantize size to reduce swimming
    // Note: if size is uniform and there is no focusing, quantization is unnecessary
    if (parameters.nonUniform_)
    {
        viewSize.x_ = ceilf(sqrtf(viewSize.x_ / parameters.quantize_));
        viewSize.y_ = ceilf(sqrtf(viewSize.y_ / parameters.quantize_));
        viewSize.x_ = Max(viewSize.x_ * viewSize.x_ * parameters.quantize_, parameters.minView_);
        viewSize.y_ = Max(viewSize.y_ * viewSize.y_ * parameters.quantize_, parameters.minView_);
    }
    else if (parameters.focus_)
    {
        viewSize.x_ = Max(viewSize.x_, viewSize.y_);
        viewSize.x_ = ceilf(sqrtf(viewSize.x_ / parameters.quantize_));
        viewSize.x_ = Max(viewSize.x_ * viewSize.x_ * parameters.quantize_, parameters.minView_);
        viewSize.y_ = viewSize.x_;
    }

    shadowCamera_->SetOrthoSize(viewSize);

    // Center shadow camera to the view space bounding box
    const Quaternion rot(shadowCameraNode->GetWorldRotation());
    const Vector3 adjust(center.x_, center.y_, 0.0f);
    shadowCameraNode->Translate(rot * adjust, TS_WORLD);

    // If the shadow map viewport is known, snap to whole texels
    if (shadowMapWidth > 0.0f)
    {
        const Vector3 viewPos(rot.Inverse() * shadowCameraNode->GetWorldPosition());
        // Take into account that shadow map border will not be used
        const float invActualSize = 1.0f / (shadowMapWidth - 2.0f);
        const Vector2 texelSize(viewSize.x_ * invActualSize, viewSize.y_ * invActualSize);
        const Vector3 snap(-fmodf(viewPos.x_, texelSize.x_), -fmodf(viewPos.y_, texelSize.y_), 0.0f);
        shadowCameraNode->Translate(rot * snap, TS_WORLD);
    }
}

void SceneLight::BeginFrame(bool hasShadow)
{
    litGeometries_.clear();
    tempShadowCasters_.clear();
    hasShadow_ = hasShadow;
    MarkPipelineStateHashDirty();
}

void SceneLight::UpdateLitGeometriesAndShadowCasters(SceneLightProcessContext& ctx)
{
    CollectLitGeometriesAndMaybeShadowCasters(ctx);

    const LightType lightType = light_->GetLightType();
    Camera* cullCamera = ctx.frameInfo_.camera_;
    Octree* octree = ctx.frameInfo_.octree_;
    const Frustum& frustum = cullCamera->GetFrustum();

    if (hasShadow_)
    {
        SetupShadowCameras(ctx);

        // Process each split for shadow casters
        for (unsigned i = 0; i < numSplits_; ++i)
        {
            Camera* shadowCamera = splits_[i].shadowCamera_;
            const Frustum& shadowCameraFrustum = shadowCamera->GetFrustum();
            splits_[i].shadowCasters_.clear();
            splits_[i].shadowCasterBatches_.clear();

            // For point light check that the face is visible: if not, can skip the split
            if (lightType == LIGHT_POINT && frustum.IsInsideFast(BoundingBox(shadowCameraFrustum)) == OUTSIDE)
                continue;

            // For directional light check that the split is inside the visible scene: if not, can skip the split
            if (lightType == LIGHT_DIRECTIONAL)
            {
                if (!ctx.sceneZRange_.Interset(splits_[i].zRange_))
                    continue;

                // Reuse lit geometry query for all except directional lights
                DirectionalLightShadowCasterOctreeQuery query(
                    tempShadowCasters_, shadowCameraFrustum, DRAWABLE_GEOMETRY, light_, cullCamera->GetViewMask());
                octree->GetDrawables(query);
            }

            // Check which shadow casters actually contribute to the shadowing
            ProcessShadowCasters(ctx, tempShadowCasters_, i);
        }
    }
}

void SceneLight::FinalizeShadowMap()
{
    // Skip if doesn't have shadow or shadow casters
    if (!hasShadow_)
        return;

    const auto hasShadowCaster = [](const SceneLightShadowSplit& split) { return !split.shadowCasters_.empty(); };
    if (!ea::any_of(ea::begin(splits_), ea::begin(splits_) + numSplits_, hasShadowCaster))
    {
        hasShadow_ = false;
        return;
    }

    // Evaluate split shadow map size
    // TODO(renderer): Implement me
    shadowMapSplitSize_ = 1024;
    shadowMapSize_ = IntVector2{ shadowMapSplitSize_, shadowMapSplitSize_ } * GetSplitsGridSize();
}

void SceneLight::SetShadowMap(const ShadowMap& shadowMap)
{
    // If failed to allocate, reset shadows
    if (!shadowMap.texture_)
    {
        numSplits_ = 0;
        return;
    }

    // Initialize shadow map for all splits
    for (unsigned splitIndex = 0; splitIndex < numSplits_; ++splitIndex)
        splits_[splitIndex].shadowMap_ = shadowMap.GetSplit(splitIndex, GetSplitsGridSize());
}

unsigned SceneLight::RecalculatePipelineStateHash() const
{
    // TODO(renderer): Extract into pipeline state factory
    unsigned hash = 0;
    hash |= light_->GetLightType() & 0x3;
    hash |= static_cast<unsigned>(hasShadow_) << 2;
    hash |= static_cast<unsigned>(!!light_->GetShapeTexture()) << 3;
    hash |= static_cast<unsigned>(light_->GetSpecularIntensity() > 0.0f) << 4;
    hash |= static_cast<unsigned>(light_->GetShadowBias().normalOffset_ > 0.0f) << 5;
    return hash;
}

void SceneLight::CollectLitGeometriesAndMaybeShadowCasters(SceneLightProcessContext& ctx)
{
    Octree* octree = ctx.frameInfo_.octree_;
    switch (light_->GetLightType())
    {
    case LIGHT_SPOT:
    {
        SpotLightLitGeometriesQuery query(litGeometries_, hasShadow_ ? &tempShadowCasters_ : nullptr,
            *ctx.drawableData_, light_, ctx.frameInfo_.camera_->GetViewMask());
        octree->GetDrawables(query);
        break;
    }
    case LIGHT_POINT:
    {
        PointLightLitGeometriesQuery query(litGeometries_, hasShadow_ ? &tempShadowCasters_ : nullptr,
            *ctx.drawableData_, light_, ctx.frameInfo_.camera_->GetViewMask());
        octree->GetDrawables(query);
        break;
    }
    case LIGHT_DIRECTIONAL:
    {
        const unsigned lightMask = light_->GetLightMask();
        ctx.visibleGeometries_->ForEach([&](unsigned, unsigned, Drawable* drawable)
        {
            if (drawable->GetLightMask() & lightMask)
                litGeometries_.push_back(drawable);
        });
        break;
    }
    }
}

Camera* SceneLight::GetOrCreateShadowCamera(unsigned split)
{
    if (!splits_[split].shadowCamera_)
    {
        auto node = MakeShared<Node>(light_->GetContext());
        auto camera = node->CreateComponent<Camera>();
        camera->SetOrthographic(false);
        camera->SetZoom(1.0f);
        splits_[split].shadowCameraNode_ = node;
        splits_[split].shadowCamera_ = camera;
    }
    return splits_[split].shadowCamera_;
}

void SceneLight::SetupShadowCameras(SceneLightProcessContext& ctx)
{
    Camera* cullCamera = ctx.frameInfo_.camera_;

    switch (light_->GetLightType())
    {
    case LIGHT_DIRECTIONAL:
    {
        const CascadeParameters& cascade = light_->GetShadowCascade();

        float nearSplit = cullCamera->GetNearClip();
        const int numSplits = light_->GetNumShadowSplits();

        numSplits_ = 0;
        for (unsigned i = 0; i < numSplits; ++i)
        {
            // If split is completely beyond camera far clip, we are done
            if (nearSplit > cullCamera->GetFarClip())
                break;

            const float farSplit = Min(cullCamera->GetFarClip(), cascade.splits_[i]);
            if (farSplit <= nearSplit)
                break;

            // Setup the shadow camera for the split
            Camera* shadowCamera = GetOrCreateShadowCamera(i);
            splits_[i].zRange_ = { nearSplit, farSplit };
            splits_[i].SetupDirLightShadowCamera(light_, ctx.frameInfo_.camera_, litGeometries_,
                ctx.sceneZRange_, ctx.drawableData_->zRange_);

            nearSplit = farSplit;
            ++numSplits_;
        }
        break;
    }
    case LIGHT_SPOT:
    {
        Camera* shadowCamera = GetOrCreateShadowCamera(0);
        Node* cameraNode = shadowCamera->GetNode();
        Node* lightNode = light_->GetNode();

        cameraNode->SetTransform(lightNode->GetWorldPosition(), lightNode->GetWorldRotation());
        shadowCamera->SetNearClip(light_->GetShadowNearFarRatio() * light_->GetRange());
        shadowCamera->SetFarClip(light_->GetRange());
        shadowCamera->SetFov(light_->GetFov());
        shadowCamera->SetAspectRatio(light_->GetAspectRatio());

        numSplits_ = 1;
        break;
    }
    case LIGHT_POINT:
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

        for (unsigned i = 0; i < MAX_CUBEMAP_FACES; ++i)
        {
            Camera* shadowCamera = GetOrCreateShadowCamera(i);
            Node* cameraNode = shadowCamera->GetNode();

            // When making a shadowed point light, align the splits along X, Y and Z axes regardless of light rotation
            cameraNode->SetPosition(light_->GetNode()->GetWorldPosition());
            cameraNode->SetDirection(*directions[i]);
            shadowCamera->SetNearClip(light_->GetShadowNearFarRatio() * light_->GetRange());
            shadowCamera->SetFarClip(light_->GetRange());
            shadowCamera->SetFov(90.0f);
            shadowCamera->SetAspectRatio(1.0f);
        }

        numSplits_ = MAX_CUBEMAP_FACES;
        break;
    }
    }
}

bool SceneLight::IsShadowCasterVisible(SceneLightProcessContext& ctx,
    Drawable* drawable, BoundingBox lightViewBox, Camera* shadowCamera, const Matrix3x4& lightView,
    const Frustum& lightViewFrustum, const BoundingBox& lightViewFrustumBox)
{
    if (shadowCamera->IsOrthographic())
    {
        // Extrude the light space bounding box up to the far edge of the frustum's light space bounding box
        lightViewBox.max_.z_ = Max(lightViewBox.max_.z_, lightViewFrustumBox.max_.z_);
        return lightViewFrustum.IsInsideFast(lightViewBox) != OUTSIDE;
    }
    else
    {
        // If light is not directional, can do a simple check: if object is visible, its shadow is too
        const unsigned drawableIndex = drawable->GetDrawableIndex();
        if (ctx.drawableData_->traits_[drawableIndex] & SceneDrawableData::DrawableVisibleGeometry)
            return true;

        // For perspective lights, extrusion direction depends on the position of the shadow caster
        Vector3 center = lightViewBox.Center();
        Ray extrusionRay(center, center);

        float extrusionDistance = shadowCamera->GetFarClip();
        float originalDistance = Clamp(center.Length(), M_EPSILON, extrusionDistance);

        // Because of the perspective, the bounding box must also grow when it is extruded to the distance
        float sizeFactor = extrusionDistance / originalDistance;

        // Calculate the endpoint box and merge it to the original. Because it's axis-aligned, it will be larger
        // than necessary, so the test will be conservative
        Vector3 newCenter = extrusionDistance * extrusionRay.direction_;
        Vector3 newHalfSize = lightViewBox.Size() * sizeFactor * 0.5f;
        BoundingBox extrudedBox(newCenter - newHalfSize, newCenter + newHalfSize);
        lightViewBox.Merge(extrudedBox);

        return lightViewFrustum.IsInsideFast(lightViewBox) != OUTSIDE;
    }
}

void SceneLight::ProcessShadowCasters(SceneLightProcessContext& ctx,
    const ea::vector<Drawable*>& drawables, unsigned splitIndex)
{
    const unsigned workerThreadIndex = WorkQueue::GetWorkerThreadIndex();
    unsigned lightMask = light_->GetLightMaskEffective();
    Camera* cullCamera = ctx.frameInfo_.camera_;

    Camera* shadowCamera = splits_[splitIndex].shadowCamera_;
    const Frustum& shadowCameraFrustum = shadowCamera->GetFrustum();
    const Matrix3x4& lightView = shadowCamera->GetView();
    const Matrix4& lightProj = shadowCamera->GetProjection();
    LightType type = light_->GetLightType();

    splits_[splitIndex].shadowCasterBox_.Clear();

    // Transform scene frustum into shadow camera's view space for shadow caster visibility check. For point & spot lights,
    // we can use the whole scene frustum. For directional lights, use the intersection of the scene frustum and the split
    // frustum, so that shadow casters do not get rendered into unnecessary splits
    Frustum lightViewFrustum;
    if (type != LIGHT_DIRECTIONAL)
        lightViewFrustum = cullCamera->GetSplitFrustum(ctx.sceneZRange_.first, ctx.sceneZRange_.second).Transformed(lightView);
    else
    {
        const DrawableZRange splitZRange = ctx.sceneZRange_ & splits_[splitIndex].zRange_;
        lightViewFrustum = cullCamera->GetSplitFrustum(splitZRange.first, splitZRange.second).Transformed(lightView);
    }

    BoundingBox lightViewFrustumBox(lightViewFrustum);

    // Check for degenerate split frustum: in that case there is no need to get shadow casters
    if (lightViewFrustum.vertices_[0] == lightViewFrustum.vertices_[4])
        return;

    BoundingBox lightViewBox;
    BoundingBox lightProjBox;

    for (auto i = drawables.begin(); i != drawables.end(); ++i)
    {
        Drawable* drawable = *i;
        // In case this is a point or spot light query result reused for optimization, we may have non-shadowcasters included.
        // Check for that first
        if (!drawable->GetCastShadows())
            continue;
        // Check shadow mask
        if (!(drawable->GetShadowMask() & lightMask))
            continue;
        // For point light, check that this drawable is inside the split shadow camera frustum
        if (type == LIGHT_POINT && shadowCameraFrustum.IsInsideFast(drawable->GetWorldBoundingBox()) == OUTSIDE)
            continue;

        // Check shadow distance
        // Note: as lights are processed threaded, it is possible a drawable's UpdateBatches() function is called several
        // times. However, this should not cause problems as no scene modification happens at this point.
        const unsigned drawableIndex = drawable->GetDrawableIndex();
        const bool isUpdated = ctx.drawableData_->isUpdated_[drawableIndex].test_and_set(std::memory_order_relaxed);
        if (!isUpdated)
            ctx.geometriesToBeUpdates_->Insert(workerThreadIndex, drawable);

        // Project shadow caster bounding box to light view space for visibility check
        lightViewBox = drawable->GetWorldBoundingBox().Transformed(lightView);

        if (IsShadowCasterVisible(ctx, drawable, lightViewBox, shadowCamera, lightView, lightViewFrustum, lightViewFrustumBox))
        {
            // Merge to shadow caster bounding box (only needed for focused spot lights) and add to the list
            if (type == LIGHT_SPOT && light_->GetShadowFocus().focus_)
            {
                lightProjBox = lightViewBox.Projected(lightProj);
                splits_[splitIndex].shadowCasterBox_.Merge(lightProjBox);
            }

            splits_[splitIndex].shadowCasters_.push_back(drawable);
        }
    }
}

IntVector2 SceneLight::GetSplitsGridSize() const
{
    if (numSplits_ == 1)
        return { 1, 1 };
    else if (numSplits_ == 2)
        return { 2, 1 };
    else if (numSplits_ < 6)
        return { 2, 2 };
    else
        return { 3, 2 };
}

}
