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
        const SceneDrawableData& transientData, Light* light)
        : SphereOctreeQuery(result, GetLightSphere(light), DRAWABLE_GEOMETRY)
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
            const unsigned drawableIndex = drawable->GetDrawableIndex();
            const unsigned traits = transientData_->traits_[drawableIndex];
            if (traits & SceneDrawableData::DrawableVisibleGeometry)
            {
                if (drawable->GetLightMask() & lightMask_)
                {
                    if (inside || sphere_.IsInsideFast(drawable->GetWorldBoundingBox()))
                    {
                        result_.push_back(drawable);
                        if (shadowCasters_ && drawable->GetCastShadows())
                            shadowCasters_->push_back(drawable);
                    }
                }
            }
        }
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
        const SceneDrawableData& transientData, Light* light)
        : FrustumOctreeQuery(result, light->GetFrustum(), DRAWABLE_GEOMETRY)
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
            const unsigned drawableIndex = drawable->GetDrawableIndex();
            const unsigned traits = transientData_->traits_[drawableIndex];
            if (traits & SceneDrawableData::DrawableVisibleGeometry)
            {
                if (drawable->GetLightMask() & lightMask_)
                {
                    if (inside || frustum_.IsInsideFast(drawable->GetWorldBoundingBox()))
                    {
                        result_.push_back(drawable);
                        if (shadowCasters_ && drawable->GetCastShadows())
                            shadowCasters_->push_back(drawable);
                    }
                }
            }
        }
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
        const Frustum& frustum, DrawableFlags drawableFlags, unsigned viewMask)
        : FrustumOctreeQuery(result, frustum, drawableFlags, viewMask)
    {
    }

    /// Intersection test for drawables.
    void TestDrawables(Drawable** start, Drawable** end, bool inside) override
    {
        for (Drawable* drawable : MakeIteratorRange(start, end))
        {
            if (drawable->GetCastShadows() && (drawable->GetDrawableFlags() & drawableFlags_) &&
                (drawable->GetViewMask() & viewMask_))
            {
                if (inside || frustum_.IsInsideFast(drawable->GetWorldBoundingBox()))
                    result_.push_back(drawable);
            }
        }
    }
};

}

void SceneLight::BeginFrame(bool hasShadow)
{
    litGeometries_.clear();
    hasShadow_ = hasShadow && light_->GetCastShadows();
    MarkPipelineStateHashDirty();
}

void SceneLight::Process(SceneLightProcessContext& ctx)
{
    const LightType lightType = light_->GetLightType();
    Camera* cullCamera = ctx.frameInfo_.camera_;
    Octree* octree = ctx.frameInfo_.octree_;
    const Frustum& frustum = cullCamera->GetFrustum();

    // Update lit geometries
    switch (lightType)
    {
    case LIGHT_SPOT:
    {
        SpotLightLitGeometriesQuery query(
            litGeometries_, hasShadow_ ? &tempShadowCasters_ : nullptr, *ctx.drawableData_, light_);
        octree->GetDrawables(query);
        break;
    }
    case LIGHT_POINT:
    {
        PointLightLitGeometriesQuery query(
            litGeometries_, hasShadow_ ? &tempShadowCasters_ : nullptr, *ctx.drawableData_, light_);
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

    if (hasShadow_)
    {
        SetupShadowCameras(ctx);

        // Process each split for shadow casters
        shadowCasters_.clear();
        for (unsigned i = 0; i < numSplits_; ++i)
        {
            Camera* shadowCamera = shadowCameras_[i];
            const Frustum& shadowCameraFrustum = shadowCamera->GetFrustum();
            shadowCasterBegin_[i] = shadowCasterEnd_[i] = shadowCasters_.size();

            // For point light check that the face is visible: if not, can skip the split
            if (lightType == LIGHT_POINT && frustum.IsInsideFast(BoundingBox(shadowCameraFrustum)) == OUTSIDE)
                continue;

            // For directional light check that the split is inside the visible scene: if not, can skip the split
            if (lightType == LIGHT_DIRECTIONAL)
            {
                if (ctx.sceneZRange_.first > shadowFarSplits_[i])
                    continue;
                if (ctx.sceneZRange_.second < shadowNearSplits_[i])
                    continue;

                // Reuse lit geometry query for all except directional lights
                DirectionalLightShadowCasterOctreeQuery query(
                    tempShadowCasters_, shadowCameraFrustum, DRAWABLE_GEOMETRY, cullCamera->GetViewMask());
                octree->GetDrawables(query);
            }

            // Check which shadow casters actually contribute to the shadowing
            ProcessShadowCasters(ctx, tempShadowCasters_, i);
        }
    }
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

Camera* SceneLight::GetOrCreateShadowCamera(unsigned split)
{
    if (!shadowCameras_[split])
    {
        auto node = MakeShared<Node>(light_->GetContext());
        auto camera = node->CreateComponent<Camera>();
        camera->SetOrthographic(false);
        camera->SetZoom(1.0f);
        shadowCameraNodes_[split] = node;
        shadowCameras_[split] = camera;
    }
    return shadowCameras_[split];
}

void SceneLight::SetupShadowCameras(SceneLightProcessContext& ctx)
{
    Camera* cullCamera = ctx.frameInfo_.camera_;
    numSplits_ = 0;

    switch (light_->GetLightType())
    {
    case LIGHT_DIRECTIONAL:
    {
        const CascadeParameters& cascade = light_->GetShadowCascade();

        float nearSplit = cullCamera->GetNearClip();
        float farSplit;
        int numSplits = light_->GetNumShadowSplits();

        while (numSplits_ < numSplits)
        {
            // If split is completely beyond camera far clip, we are done
            if (nearSplit > cullCamera->GetFarClip())
                break;

            farSplit = Min(cullCamera->GetFarClip(), cascade.splits_[numSplits_]);
            if (farSplit <= nearSplit)
                break;

            // Setup the shadow camera for the split
            Camera* shadowCamera = GetOrCreateShadowCamera(numSplits_);
            shadowNearSplits_[numSplits_] = nearSplit;
            shadowFarSplits_[numSplits_] = farSplit;
            SetupDirLightShadowCamera(ctx, shadowCamera, nearSplit, farSplit);

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

void SceneLight::SetupDirLightShadowCamera(SceneLightProcessContext& ctx,
    Camera* shadowCamera, float nearSplit, float farSplit)
{
    Node* shadowCameraNode = shadowCamera->GetNode();
    Camera* cullCamera = ctx.frameInfo_.camera_;
    Node* lightNode = light_->GetNode();
    float extrusionDistance = Min(cullCamera->GetFarClip(), light_->GetShadowMaxExtrusion());
    const FocusParameters& parameters = light_->GetShadowFocus();

    // Calculate initial position & rotation
    Vector3 pos = cullCamera->GetNode()->GetWorldPosition() - extrusionDistance * lightNode->GetWorldDirection();
    shadowCameraNode->SetTransform(pos, lightNode->GetWorldRotation());

    // Calculate main camera shadowed frustum in light's view space
    farSplit = Min(farSplit, cullCamera->GetFarClip());
    // Use the scene Z bounds to limit frustum size if applicable
    if (parameters.focus_)
    {
        nearSplit = Max(ctx.sceneZRange_.first, nearSplit);
        farSplit = Min(ctx.sceneZRange_.second, farSplit);
    }

    Frustum splitFrustum = cullCamera->GetSplitFrustum(nearSplit, farSplit);
    Polyhedron frustumVolume;
    frustumVolume.Define(splitFrustum);
    // If focusing enabled, clip the frustum volume by the combined bounding box of the lit geometries within the frustum
    if (parameters.focus_)
    {
        BoundingBox litGeometriesBox;
        unsigned lightMask = light_->GetLightMaskEffective();

        for (Drawable* drawable : litGeometries_)
        {
            const DrawableZRange& drawableZRange = ctx.drawableData_->zRange_[drawable->GetDrawableIndex()];
            if (drawableZRange.first <= farSplit && drawableZRange.second >= nearSplit)
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
    const Matrix3x4& lightView = shadowCamera->GetView();
    frustumVolume.Transform(lightView);

    // Fit the frustum volume inside a bounding box. If uniform size, use a sphere instead
    BoundingBox shadowBox;
    if (!parameters.nonUniform_)
        shadowBox.Define(Sphere(frustumVolume));
    else
        shadowBox.Define(frustumVolume);

    shadowCamera->SetOrthographic(true);
    shadowCamera->SetAspectRatio(1.0f);
    shadowCamera->SetNearClip(0.0f);
    shadowCamera->SetFarClip(shadowBox.max_.z_);

    // Center shadow camera on the bounding box. Can not snap to texels yet as the shadow map viewport is unknown
    QuantizeDirLightShadowCamera(ctx, shadowCamera, IntRect(0, 0, 0, 0), shadowBox);
}

void SceneLight::QuantizeDirLightShadowCamera(SceneLightProcessContext& ctx,
    Camera* shadowCamera, const IntRect& shadowViewport, const BoundingBox& viewBox)
{
    Node* shadowCameraNode = shadowCamera->GetNode();
    const FocusParameters& parameters = light_->GetShadowFocus();
    auto shadowMapWidth = (float)(shadowViewport.Width());

    float minX = viewBox.min_.x_;
    float minY = viewBox.min_.y_;
    float maxX = viewBox.max_.x_;
    float maxY = viewBox.max_.y_;

    Vector2 center((minX + maxX) * 0.5f, (minY + maxY) * 0.5f);
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

    shadowCamera->SetOrthoSize(viewSize);

    // Center shadow camera to the view space bounding box
    Quaternion rot(shadowCameraNode->GetWorldRotation());
    Vector3 adjust(center.x_, center.y_, 0.0f);
    shadowCameraNode->Translate(rot * adjust, TS_WORLD);

    // If the shadow map viewport is known, snap to whole texels
    if (shadowMapWidth > 0.0f)
    {
        Vector3 viewPos(rot.Inverse() * shadowCameraNode->GetWorldPosition());
        // Take into account that shadow map border will not be used
        float invActualSize = 1.0f / (shadowMapWidth - 2.0f);
        Vector2 texelSize(viewSize.x_ * invActualSize, viewSize.y_ * invActualSize);
        Vector3 snap(-fmodf(viewPos.x_, texelSize.x_), -fmodf(viewPos.y_, texelSize.y_), 0.0f);
        shadowCameraNode->Translate(rot * snap, TS_WORLD);
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
    unsigned lightMask = light_->GetLightMaskEffective();
    Camera* cullCamera = ctx.frameInfo_.camera_;

    Camera* shadowCamera = shadowCameras_[splitIndex];
    const Frustum& shadowCameraFrustum = shadowCamera->GetFrustum();
    const Matrix3x4& lightView = shadowCamera->GetView();
    const Matrix4& lightProj = shadowCamera->GetProjection();
    LightType type = light_->GetLightType();

    shadowCasterBox_[splitIndex].Clear();

    // Transform scene frustum into shadow camera's view space for shadow caster visibility check. For point & spot lights,
    // we can use the whole scene frustum. For directional lights, use the intersection of the scene frustum and the split
    // frustum, so that shadow casters do not get rendered into unnecessary splits
    Frustum lightViewFrustum;
    if (type != LIGHT_DIRECTIONAL)
        lightViewFrustum = cullCamera->GetSplitFrustum(ctx.sceneZRange_.first, ctx.sceneZRange_.second).Transformed(lightView);
    else
        lightViewFrustum = cullCamera->GetSplitFrustum(Max(ctx.sceneZRange_.first, shadowNearSplits_[splitIndex]),
            Min(ctx.sceneZRange_.second, shadowFarSplits_[splitIndex])).Transformed(lightView);

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
        if (!(ctx.drawableData_->traits_[drawableIndex] & SceneDrawableData::DrawableVisibleGeometry))
            drawable->UpdateBatches(ctx.frameInfo_);
        float maxShadowDistance = drawable->GetShadowDistance();
        float drawDistance = drawable->GetDrawDistance();
        if (drawDistance > 0.0f && (maxShadowDistance <= 0.0f || drawDistance < maxShadowDistance))
            maxShadowDistance = drawDistance;
        if (maxShadowDistance > 0.0f && drawable->GetDistance() > maxShadowDistance)
            continue;

        // Project shadow caster bounding box to light view space for visibility check
        lightViewBox = drawable->GetWorldBoundingBox().Transformed(lightView);

        if (IsShadowCasterVisible(ctx, drawable, lightViewBox, shadowCamera, lightView, lightViewFrustum, lightViewFrustumBox))
        {
            // Merge to shadow caster bounding box (only needed for focused spot lights) and add to the list
            if (type == LIGHT_SPOT && light_->GetShadowFocus().focus_)
            {
                lightProjBox = lightViewBox.Projected(lightProj);
                shadowCasterBox_[splitIndex].Merge(lightProjBox);
            }

            const auto& sourceBatches = drawable->GetBatches();
            for (unsigned j = 0; j < sourceBatches.size(); ++j)
            {
                // TODO(renderer): Optimize
                const SourceBatch& sourceBatch = sourceBatches[j];
                Renderer* renderer = light_->GetContext()->GetRenderer();
                Material* material = sourceBatch.material_ ? sourceBatch.material_ : renderer->GetDefaultMaterial();
                const ea::vector<TechniqueEntry>& techniques = material->GetTechniques();
                Technique* tech = techniques[0].technique_;
                Pass* pass = tech->GetSupportedPass(Technique::shadowPassIndex);
                if (!pass)
                    continue;

                BaseSceneBatch batch;
                batch.drawableIndex_ = drawable->GetDrawableIndex();
                batch.sourceBatchIndex_ = j;
                batch.geometryType_ = sourceBatch.geometryType_;
                batch.drawable_ = drawable;
                batch.geometry_ = sourceBatch.geometry_;
                batch.material_ = sourceBatch.material_;
                batch.pass_ = pass;
                shadowCasters_.push_back(batch);
                //shadowCasters_.push_back(drawable);
            }
        }
    }

    shadowCasterEnd_[splitIndex] = shadowCasters_.size();
}

}
