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

#include "../Core/WorkQueue.h"
#include "../Graphics/Drawable.h"
#include "../Graphics/GlobalIllumination.h"
#include "../Graphics/OcclusionBuffer.h"
#include "../Graphics/Octree.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/TextureCube.h"
#include "../Graphics/Zone.h"
#include "../IO/Log.h"
#include "../RenderPipeline/DrawableProcessor.h"
#include "../RenderPipeline/LightProcessor.h"
#include "../RenderPipeline/RenderPipelineDefs.h"
#include "../Scene/Scene.h"

#include <EASTL/sort.h>

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

/// Calculate light penalty for drawable for given absolute light penalty and light settings
/// Order of penalties, from lower to higher:
/// -2:      Important directional lights;
/// -1:      Important point and spot lights;
///  0 .. 2: Automatic lights;
///  3 .. 5: Not important lights.
float GetDrawableLightPenalty(float intensityPenalty, bool isNegative, LightImportance importance, LightType type)
{
    switch (importance)
    {
    case LI_IMPORTANT:
        return type == LIGHT_DIRECTIONAL && !isNegative ? -2 : -1;
    case LI_AUTO:
        return intensityPenalty <= 1.0f ? intensityPenalty : 2.0f - 1.0f / intensityPenalty;
    case LI_NOT_IMPORTANT:
        return intensityPenalty <= 1.0f ? 3.0f + intensityPenalty : 5.0f - 1.0f / intensityPenalty;
    default:
        assert(0);
        return M_LARGE_VALUE;
    }
}

/// Return whether shadow of bounding box is inside frustum (orthogonal light source).
bool IsBoundingBoxShadowInOrthoFrustum(const BoundingBox& boundingBox,
    const Frustum& frustum, const BoundingBox& frustumBoundingBox)
{
    // Extrude the bounding box up to the far edge of the frustum's light space bounding box
    BoundingBox extrudedBoundingBox = boundingBox;
    extrudedBoundingBox.max_.z_ = Max(extrudedBoundingBox.max_.z_, frustumBoundingBox.max_.z_);
    return frustum.IsInsideFast(extrudedBoundingBox) != OUTSIDE;
}

/// Return whether shadow of bounding box is inside frustum (perspective light source).
bool IsBoundingBoxShadowInPerspectiveFrustum(const BoundingBox& boundingBox,
    const Frustum& frustum, float extrusionDistance)
{
    // Extrusion direction depends on the position of the shadow caster
    const Vector3 center = boundingBox.Center();
    const Ray extrusionRay(center, center);

    // Because of the perspective, the bounding box must also grow when it is extruded to the distance
    const float originalDistance = Clamp(center.Length(), M_EPSILON, extrusionDistance);
    const float sizeFactor = extrusionDistance / originalDistance;

    // Calculate the endpoint box and merge it to the original. Because it's axis-aligned, it will be larger
    // than necessary, so the test will be conservative
    const Vector3 newCenter = extrusionDistance * extrusionRay.direction_;
    const Vector3 newHalfSize = boundingBox.Size() * sizeFactor * 0.5f;

    BoundingBox extrudedBox(newCenter - newHalfSize, newCenter + newHalfSize);
    extrudedBox.Merge(boundingBox);

    return frustum.IsInsideFast(extrudedBox) != OUTSIDE;
}

/// Return whether the shadow caster is visible.
bool IsShadowCasterVisible(const BoundingBox& lightSpaceBoundingBox, Camera* shadowCamera,
    const Frustum& lightSpaceFrustum, const BoundingBox& lightSpaceFrustumBoundingBox)
{
    if (shadowCamera->IsOrthographic())
        return IsBoundingBoxShadowInOrthoFrustum(lightSpaceBoundingBox, lightSpaceFrustum, lightSpaceFrustumBoundingBox);
    else
        return IsBoundingBoxShadowInPerspectiveFrustum(lightSpaceBoundingBox, lightSpaceFrustum, shadowCamera->GetFarClip());
}

}

DrawableProcessorPass::DrawableProcessorPass(RenderPipelineInterface* renderPipeline, DrawableProcessorPassFlags flags,
    unsigned deferredPassIndex, unsigned unlitBasePassIndex, unsigned litBasePassIndex, unsigned lightPassIndex)
    : Object(renderPipeline->GetContext())
    , flags_(flags)
    , deferredPassIndex_(deferredPassIndex)
    , unlitBasePassIndex_(unlitBasePassIndex)
    , litBasePassIndex_(litBasePassIndex)
    , lightPassIndex_(lightPassIndex)
{
    renderPipeline->OnUpdateBegin.Subscribe(this, &DrawableProcessorPass::OnUpdateBegin);
}

DrawableProcessorPass::AddBatchResult DrawableProcessorPass::AddBatch(unsigned threadIndex,
    Drawable* drawable, unsigned sourceBatchIndex, Technique* technique)
{
    if (Pass* deferredPass = technique->GetPass(deferredPassIndex_))
    {
        geometryBatches_.PushBack(threadIndex, { drawable, sourceBatchIndex, deferredPass, nullptr, nullptr, nullptr });
        return { true, false };
    }

    Pass* unlitBasePass = technique->GetPass(unlitBasePassIndex_);
    Pass* lightPass = technique->GetPass(lightPassIndex_);
    Pass* litBasePass = lightPass ? technique->GetPass(litBasePassIndex_) : nullptr;

    if (!unlitBasePass)
        return { false, false };

    geometryBatches_.PushBack(threadIndex, { drawable, sourceBatchIndex, nullptr, unlitBasePass, litBasePass, lightPass });
    return { true, !!lightPass };
}

void DrawableProcessorPass::OnUpdateBegin(const CommonFrameInfo& frameInfo)
{
    geometryBatches_.Clear();
}

DrawableProcessor::DrawableProcessor(RenderPipelineInterface* renderPipeline)
    : Object(renderPipeline->GetContext())
    , workQueue_(GetSubsystem<WorkQueue>())
    , defaultMaterial_(GetSubsystem<Renderer>()->GetDefaultMaterial())
    , lightProcessorCache_(ea::make_unique<LightProcessorCache>())
{
    renderPipeline->OnCollectStatistics.Subscribe(this, &DrawableProcessor::OnCollectStatistics);
}

DrawableProcessor::~DrawableProcessor()
{
}

void DrawableProcessor::SetPasses(ea::vector<SharedPtr<DrawableProcessorPass>> passes)
{
    passes_ = ea::move(passes);
}

void DrawableProcessor::SetSettings(const DrawableProcessorSettings& settings)
{
    settings_ = settings;
    lightProcessorCache_->SetSettings(settings_.lightProcessorCache_);
}

void DrawableProcessor::OnUpdateBegin(const FrameInfo& frameInfo)
{
    // Initialize frame constants
    frameInfo_ = frameInfo;
    numDrawables_ = frameInfo_.octree_->GetAllDrawables().size();
    cullCameraViewMatrix_ = frameInfo_.camera_->GetView();
    cullCameraZAxis_ = { cullCameraViewMatrix_.m20_, cullCameraViewMatrix_.m21_, cullCameraViewMatrix_.m22_ };
    cullCameraZAxisAbs_ = cullCameraZAxis_.Abs();

    materialQuality_ = settings_.materialQuality_;
    if (frameInfo_.camera_->GetViewOverrideFlags() & VO_LOW_MATERIAL_QUALITY)
        materialQuality_ = QUALITY_LOW;

    gi_ = frameInfo_.scene_->GetComponent<GlobalIllumination>();

    // Clean temporary containers
    sceneZRangeTemp_.clear();
    sceneZRangeTemp_.resize(WorkQueue::GetMaxThreadIndex());
    sceneZRange_ = {};

    isDrawableUpdated_.resize(numDrawables_);
    for (UpdateFlag& isUpdated : isDrawableUpdated_)
        isUpdated.clear(std::memory_order_relaxed);

    geometryFlags_.resize(numDrawables_);
    ea::fill(geometryFlags_.begin(), geometryFlags_.end(), 0);

    geometryZRanges_.resize(numDrawables_);
    geometryLighting_.resize(numDrawables_);

    sortedOccluders_.clear();
    geometries_.Clear();
    threadedGeometryUpdates_.Clear();
    nonThreadedGeometryUpdates_.Clear();

    lightsTemp_.Clear();

    queuedDrawableUpdates_.Clear();

    // Update caches
    lightProcessorCache_->Update(frameInfo.timeStep_);
}

void DrawableProcessor::OnCollectStatistics(RenderPipelineStats& stats)
{
    stats.numOccluders_ += sortedOccluders_.size();
    stats.numLights_ += lights_.size();
    stats.numShadowedLights_ += numShadowedLights_;
}

void DrawableProcessor::ProcessOccluders(const ea::vector<Drawable*>& occluders, float sizeThreshold)
{
    Camera* cullCamera = frameInfo_.camera_;
    const float halfViewSize = cullCamera->GetHalfViewSize();
    const float invOrthoSize = 1.0f / cullCamera->GetOrthoSize();

    for (Drawable* drawable : occluders)
    {
        drawable->UpdateBatches(frameInfo_);

        // Skip if too far
        const float maxDistance = drawable->GetDrawDistance();
        if (maxDistance > 0.0f && drawable->GetDistance() > maxDistance)
            return;

        // Check that occluder is big enough on the screen
        const BoundingBox& boundingBox = drawable->GetWorldBoundingBox();
        const float drawableSize = boundingBox.Size().Length();
        float relativeSize = 0.0f;
        if (cullCamera->IsOrthographic())
            relativeSize = drawableSize * invOrthoSize;
        else
        {
            // Occluders which are near the camera are more useful then occluders at the end of the camera's draw distance
            const float relativeDistance = drawable->GetDistance() / cullCamera->GetFarClip();
            relativeSize = drawableSize * halfViewSize / ea::max(M_EPSILON, drawable->GetDistance() * relativeDistance);

            // Give higher priority to occluders which the camera is inside their AABB
            const Vector3& cameraPos = cullCamera->GetNode()->GetWorldPosition();
            if (boundingBox.IsInside(cameraPos))
                relativeSize *= drawableSize;
        }

        // Keep occluders larger than threshold with lowest triangle count to size ratio.
        if (relativeSize >= sizeThreshold)
        {
            const float density = drawable->GetNumOccluderTriangles() / drawableSize;
            const float penalty = density / ea::max(M_EPSILON, relativeSize);
            sortedOccluders_.push_back({ penalty, drawable });
        }
    }

    ea::sort(sortedOccluders_.begin(), sortedOccluders_.end());
}

void DrawableProcessor::ProcessVisibleDrawables(const ea::vector<Drawable*>& drawables, OcclusionBuffer* occlusionBuffer)
{
    URHO3D_PROFILE("ProcessVisibleDrawables");

    ForEachParallel(workQueue_, drawables,
        [&](unsigned /*index*/, Drawable* drawable)
    {
        if (occlusionBuffer && drawable->IsOccludee() && !occlusionBuffer->IsVisible(drawable->GetWorldBoundingBox()))
            return;

        ProcessVisibleDrawable(drawable);
    });

    // Sort lights by component ID for stability
    lights_.resize(lightsTemp_.Size());
    ea::copy(lightsTemp_.Begin(), lightsTemp_.End(), lights_.begin());
    const auto compareLightsNegativeLast = [](Light* lhs, Light* rhs)
    {
        const bool lhsNegative = lhs->IsNegative();
        const bool rhsNegative = rhs->IsNegative();
        if (lhsNegative != rhsNegative)
            return lhsNegative < rhsNegative;
        return lhs->GetID() < rhs->GetID();
    };
    ea::sort(lights_.begin(), lights_.end(), compareLightsNegativeLast);

    lightProcessors_.clear();
    for (Light* light : lights_)
        lightProcessors_.push_back(lightProcessorCache_->GetLightProcessor(light));

    // Cook lights for forward light evaluation on CPU
    lightDataForAccumulator_.resize(lights_.size());
    for (unsigned i = 0; i < lights_.size(); ++i)
        lightDataForAccumulator_[i] = LightDataForAccumulator{ lights_[i] };

    // Expand scene Z range so it's never too small
    for (const FloatRange& range : sceneZRangeTemp_)
        sceneZRange_ |= range;

    static const float minSceneZRange = 1.0f;
    if (sceneZRange_.IsValid() && sceneZRange_.second - sceneZRange_.first < minSceneZRange)
        sceneZRange_.second = sceneZRange_.first + minSceneZRange;
}

void DrawableProcessor::UpdateDrawableZone(const BoundingBox& boundingBox, Drawable* drawable) const
{
    const Vector3 drawableCenter = boundingBox.Center();
    CachedDrawableZone& cachedZone = drawable->GetMutableCachedZone();
    const float drawableCacheDistanceSquared = (cachedZone.cachePosition_ - drawableCenter).LengthSquared();

    // Force update if bounding box is invalid
    const bool forcedUpdate = !std::isfinite(drawableCacheDistanceSquared);
    if (forcedUpdate || drawableCacheDistanceSquared >= cachedZone.cacheInvalidationDistanceSquared_)
    {
        cachedZone = frameInfo_.octree_->QueryZone(drawableCenter, drawable->GetZoneMask());
        drawable->MarkPipelineStateHashDirty();
    }
}

void DrawableProcessor::QueueDrawableGeometryUpdate(unsigned threadIndex, Drawable* drawable)
{
    const UpdateGeometryType updateGeometryType = drawable->GetUpdateGeometryType();
    if (updateGeometryType == UPDATE_MAIN_THREAD)
        nonThreadedGeometryUpdates_.PushBack(threadIndex, drawable);
    else if (updateGeometryType == UPDATE_WORKER_THREAD)
        threadedGeometryUpdates_.PushBack(threadIndex, drawable);
}

void DrawableProcessor::CheckMaterialForAuxiliaryRenderSurfaces(Material* material)
{
    // Skip if already checked or not main viewport
    if (!material || material->GetAuxViewFrameNumber() == frameInfo_.frameNumber_ || frameInfo_.renderTarget_ != nullptr)
        return;

    for (const auto& item : material->GetTextures())
    {
        // Skip if not render targets
        Texture* texture = item.second;
        if (!texture || texture->GetUsage() != TEXTURE_RENDERTARGET)
            continue;

        // Have to check cube & 2D textures separately
        if (texture->GetType() == Texture2D::GetTypeStatic())
        {
            auto* tex2D = static_cast<Texture2D*>(texture);
            RenderSurface* target = tex2D->GetRenderSurface();
            if (target && target->GetUpdateMode() == SURFACE_UPDATEVISIBLE)
                target->QueueUpdate();
        }
        else if (texture->GetType() == TextureCube::GetTypeStatic())
        {
            auto* texCube = static_cast<TextureCube*>(texture);
            for (unsigned j = 0; j < MAX_CUBEMAP_FACES; ++j)
            {
                RenderSurface* target = texCube->GetRenderSurface((CubeMapFace)j);
                if (target && target->GetUpdateMode() == SURFACE_UPDATEVISIBLE)
                    target->QueueUpdate();
            }
        }
    }

    // Flag as processed so we can early-out next time we come across this material on the same frame
    material->MarkForAuxView(frameInfo_.frameNumber_);
}

void DrawableProcessor::ProcessVisibleDrawable(Drawable* drawable)
{
    const unsigned drawableIndex = drawable->GetDrawableIndex();
    const unsigned threadIndex = WorkQueue::GetThreadIndex();

    drawable->UpdateBatches(frameInfo_);
    drawable->MarkInView(frameInfo_);

    isDrawableUpdated_[drawableIndex].test_and_set(std::memory_order_relaxed);

    // Skip if too far
    const float maxDistance = drawable->GetDrawDistance();
    if (maxDistance > 0.0f && drawable->GetDistance() > maxDistance)
        return;

    // For geometries, find zone, clear lights and calculate view space Z range
    if (drawable->GetDrawableFlags() & DRAWABLE_GEOMETRY)
    {
        const BoundingBox& boundingBox = drawable->GetWorldBoundingBox();
        const FloatRange zRange = CalculateBoundingBoxZRange(boundingBox);

        // Update zone
        UpdateDrawableZone(boundingBox, drawable);

        // Do not add "infinite" objects like skybox to prevent shadow map focusing behaving erroneously
        if (!zRange.IsValid())
            geometryZRanges_[drawableIndex] = { M_LARGE_VALUE, M_LARGE_VALUE };
        else
        {
            geometryZRanges_[drawableIndex] = zRange;
            sceneZRangeTemp_[threadIndex] |= zRange;
        }

        // Collect batches
        bool isForwardLit = false;
        bool needAmbient = false;

        const auto& sourceBatches = drawable->GetBatches();
        for (unsigned sourceBatchIndex = 0; sourceBatchIndex < sourceBatches.size(); ++sourceBatchIndex)
        {
            const SourceBatch& sourceBatch = sourceBatches[sourceBatchIndex];

            // Find current technique
            Material* material = sourceBatch.material_ ? sourceBatch.material_ : defaultMaterial_;
            Technique* technique = material->FindTechnique(drawable, materialQuality_);
            if (!technique)
                continue;

            // Check for aux views
            CheckMaterialForAuxiliaryRenderSurfaces(sourceBatch.material_);

            // Update scene passes
            for (DrawableProcessorPass* pass : passes_)
            {
                // TODO: Check whether pass is supported on mobile?
                const DrawableProcessorPass::AddBatchResult result = pass->AddBatch(
                    threadIndex, drawable, sourceBatchIndex, technique);
                if (result.forwardLitAdded_)
                    isForwardLit = true;
                if (result.added_ && pass->GetFlags().Test(DrawableProcessorPassFlag::HasAmbientLighting))
                    needAmbient = true;
            }
        }

        // Process lighting
        if (needAmbient)
        {
            LightAccumulator& lightAccumulator = geometryLighting_[drawableIndex];
            const GlobalIlluminationType giType = drawable->GetGlobalIlluminationType();

            // Reset lights
            if (isForwardLit)
                lightAccumulator.ResetLights();

            // Reset SH from GI if possible/needed, reset to zero otherwise
            if (gi_ && giType >= GlobalIlluminationType::BlendLightProbes)
            {
                unsigned& hint = drawable->GetMutableLightProbeTetrahedronHint();
                const Vector3& samplePosition = boundingBox.Center();
                lightAccumulator.sphericalHarmonics_ = gi_->SampleAmbientSH(samplePosition, hint);
            }
            else
                lightAccumulator.sphericalHarmonics_ = {};

            // Apply ambient from Zone
            const CachedDrawableZone& cachedZone = drawable->GetMutableCachedZone();
            if (!cachedZone.zone_->IsBackgroundStatic())
                lightAccumulator.sphericalHarmonics_ += cachedZone.zone_->GetAmbientAndBackgroundLighting();
            else
                lightAccumulator.sphericalHarmonics_ += cachedZone.zone_->GetAmbientLighting();

            lightAccumulator.reflectionProbe_ = cachedZone.zone_->GetReflectionProbe();
        }

        // Store geometry
        geometries_.PushBack(threadIndex, drawable);

        // Update flags
        unsigned char& flag = geometryFlags_[drawableIndex];
        flag = GeometryRenderFlag::VisibleInCullCamera;
        if (needAmbient)
            flag |= GeometryRenderFlag::Lit;
        if (isForwardLit)
            flag |= GeometryRenderFlag::ForwardLit;

        // Queue geometry update
        QueueDrawableGeometryUpdate(threadIndex, drawable);
    }
    else if (drawable->GetDrawableFlags() & DRAWABLE_LIGHT)
    {
        auto light = static_cast<Light*>(drawable);
        const Color lightColor = light->GetEffectiveColor();

        // Skip lights with zero brightness or black color, skip baked lights too
        if (!lightColor.Equals(Color::BLACK) && light->GetLightMaskEffective() != 0)
            lightsTemp_.PushBack(threadIndex, light);
    }
}

void DrawableProcessor::ProcessLights(LightProcessorCallback* callback)
{
    URHO3D_PROFILE("ProcessVisibleLights");

    for (LightProcessor* lightProcessor : lightProcessors_)
        lightProcessor->BeginUpdate(this, callback);

    ForEachParallel(workQueue_, lightProcessors_,
        [&](unsigned /*index*/, LightProcessor* lightProcessor)
    {
        lightProcessor->Update(this, callback);
    });

    SortLightProcessorsByShadowMapSize();

    numShadowedLights_ = 0;
    for (LightProcessor* lightProcessor : lightProcessorsByShadowMapSize_)
    {
        lightProcessor->EndUpdate(this, callback, settings_.pcfKernelSize_);
        if (lightProcessor->HasShadow())
            ++numShadowedLights_;
    }

    SortLightProcessorsByShadowMapTexture();

    ProcessShadowCasters();
}

void DrawableProcessor::ProcessForwardLightingForLight(
    unsigned lightIndex, const ea::vector<Drawable*>& litGeometries)
{
    if (lightIndex >= lights_.size())
    {
        URHO3D_LOGERROR("Invalid light index {}", lightIndex);
        return;
    }

    Light* light = lights_[lightIndex];
    const LightType lightType = light->GetLightType();
    const float lightIntensityPenalty = 1.0f / light->GetIntensityDivisor();
    const bool hasShadow = lightProcessors_[lightIndex]->HasShadow();
    const bool isNegative = light->IsNegative();
    const LightImportance lightImportance = hasShadow ? LI_IMPORTANT : light->GetLightImportance();

    LightAccumulatorContext ctx;
    ctx.maxVertexLights_ = settings_.maxVertexLights_;
    ctx.maxPixelLights_ = settings_.maxPixelLights_;
    ctx.lights_ = &lightDataForAccumulator_;

    ForEachParallel(workQueue_, litGeometries,
        [&](unsigned /*index*/, Drawable* geometry)
    {
        const unsigned drawableIndex = geometry->GetDrawableIndex();

        // Directional light doesn't filter out e.g. deferred lit geometries for shadow focusing
        if (lightType == LIGHT_DIRECTIONAL)
        {
            const bool isForwardLit = !!(geometryFlags_[drawableIndex] & GeometryRenderFlag::ForwardLit);
            if (!isForwardLit)
                return;
        }

        const float distance = ea::max(light->GetDistanceTo(geometry), M_LARGE_EPSILON);
        const float penalty = GetDrawableLightPenalty(distance * lightIntensityPenalty,
            isNegative, lightImportance, lightType);
        geometryLighting_[drawableIndex].AccumulateLight(ctx, geometry, lightImportance, lightIndex, penalty);
    });
}

void DrawableProcessor::FinalizeForwardLighting()
{
    ForEachParallel(workQueue_, geometries_,
        [&](unsigned /*index*/, Drawable* drawable)
    {
        const unsigned drawableIndex = drawable->GetDrawableIndex();
        const unsigned char flags = geometryFlags_[drawableIndex];
        if (flags & GeometryRenderFlag::ForwardLit)
        {
            LightAccumulator& lightAccumulator = geometryLighting_[drawableIndex];
            lightAccumulator.Cook();
        }
    });
}

void DrawableProcessor::ProcessForwardLighting()
{
    URHO3D_PROFILE("ProcessForwardLighting");

    bool hasForwardLights = false;
    for (unsigned i = 0; i < lightProcessors_.size(); ++i)
    {
        const LightProcessor* lightProcessor = lightProcessors_[i];
        if (lightProcessor->HasForwardLitGeometries())
        {
            ProcessForwardLightingForLight(i, lightProcessor->GetLitGeometries());
            hasForwardLights = true;
        }
    }
    if (hasForwardLights)
        FinalizeForwardLighting();
}

void DrawableProcessor::PreprocessShadowCasters(ea::vector<Drawable*>& shadowCasters,
    const ea::vector<Drawable*>& candidates, const FloatRange& frustumSubRange, Light* light, Camera* shadowCamera)
{
    shadowCasters.clear();

    const Frustum& shadowCameraFrustum = shadowCamera->GetFrustum();
    const Matrix3x4& worldToLightSpace = shadowCamera->GetView();
    const LightType lightType = light->GetLightType();

    // Convert frustum (or sub-frustum) to shadow camera space
    const FloatRange splitZRange = lightType != LIGHT_DIRECTIONAL ? sceneZRange_ : (sceneZRange_ & frustumSubRange);
    const Frustum frustum = frameInfo_.camera_->GetSplitFrustum(splitZRange.first, splitZRange.second);
    const Frustum lightSpaceFrustum = frustum.Transformed(worldToLightSpace);
    const BoundingBox lightSpaceFrustumBoundingBox(lightSpaceFrustum);

    // Check for degenerate split frustum: in that case there is no need to get shadow casters
    if (lightSpaceFrustum.vertices_[0] == lightSpaceFrustum.vertices_[4])
        return;

    for (Drawable* drawable : candidates)
    {
        // For point light, check that this drawable is inside the split shadow camera frustum
        if (lightType == LIGHT_POINT && shadowCameraFrustum.IsInsideFast(drawable->GetWorldBoundingBox()) == OUTSIDE)
            continue;

        // Queue shadow caster if it's visible
        const BoundingBox lightSpaceBoundingBox = drawable->GetWorldBoundingBox().Transformed(worldToLightSpace);
        const bool isDrawableVisible = !!(geometryFlags_[drawable->GetDrawableIndex()] & GeometryRenderFlag::VisibleInCullCamera);
        if (isDrawableVisible
            || IsShadowCasterVisible(lightSpaceBoundingBox, shadowCamera, lightSpaceFrustum, lightSpaceFrustumBoundingBox))
        {
            QueueDrawableUpdate(drawable);
            shadowCasters.push_back(drawable);
        }
    }
}

void DrawableProcessor::QueueDrawableUpdate(Drawable* drawable)
{
    const unsigned drawableIndex = drawable->GetDrawableIndex();
    const bool isUpdated = isDrawableUpdated_[drawableIndex].test_and_set(std::memory_order_relaxed);
    if (!isUpdated)
        queuedDrawableUpdates_.Insert(drawable);
}

void DrawableProcessor::ProcessShadowCasters()
{
    ForEachParallel(workQueue_, queuedDrawableUpdates_,
        [&](unsigned /*index*/, Drawable* drawable)
    {
        ProcessQueuedDrawable(drawable);
    });
    queuedDrawableUpdates_.Clear();
}

void DrawableProcessor::ProcessQueuedDrawable(Drawable* drawable)
{
    drawable->UpdateBatches(frameInfo_);
    drawable->MarkInView(frameInfo_);

    const BoundingBox& boundingBox = drawable->GetWorldBoundingBox();
    UpdateDrawableZone(boundingBox, drawable);
    QueueDrawableGeometryUpdate(WorkQueue::GetThreadIndex(), drawable);
}

void DrawableProcessor::SortLightProcessorsByShadowMapSize()
{
    lightProcessorsByShadowMapSize_ = lightProcessors_;

    const auto compareShadowMapSize = [](const LightProcessor* lhs, const LightProcessor* rhs)
    {
        const IntVector2 lhsSize = lhs->GetShadowMapSize();
        const IntVector2 rhsSize = rhs->GetShadowMapSize();
        if (lhsSize != rhsSize)
            return lhsSize.Length() > rhsSize.Length();
        return lhs->GetLight()->GetID() < rhs->GetLight()->GetID();
    };
    ea::sort(lightProcessorsByShadowMapSize_.begin(), lightProcessorsByShadowMapSize_.end(), compareShadowMapSize);
}

void DrawableProcessor::SortLightProcessorsByShadowMapTexture()
{
    lightProcessorsByShadowMapTexture_ = lightProcessors_;
    const auto compareShadowMapTexture = [](const LightProcessor* lhs, const LightProcessor* rhs)
    {
        return lhs->GetShadowMap().texture_ < rhs->GetShadowMap().texture_;
    };
    ea::sort(lightProcessorsByShadowMapTexture_.begin(), lightProcessorsByShadowMapTexture_.end(), compareShadowMapTexture);
}

void DrawableProcessor::UpdateGeometries()
{
    URHO3D_PROFILE("UpdateGeometries");

    // Update in worker threads
    ForEachParallel(workQueue_, threadedGeometryUpdates_,
        [&](unsigned /*index*/, Drawable* drawable)
    {
        if (drawable->GetUpdateGeometryType() == UPDATE_MAIN_THREAD)
            nonThreadedGeometryUpdates_.Insert(drawable);
        else
            drawable->UpdateGeometry(frameInfo_);
    });

    // Update in main thread
    for (Drawable* drawable : nonThreadedGeometryUpdates_)
    {
        drawable->UpdateGeometry(frameInfo_);
    };
}

FloatRange DrawableProcessor::CalculateBoundingBoxZRange(const BoundingBox& boundingBox) const
{
    const Vector3 center = boundingBox.Center();
    const Vector3 edge = boundingBox.Size() * 0.5f;

    // Ignore "infinite" objects
    if (edge.LengthSquared() >= M_LARGE_VALUE * M_LARGE_VALUE)
        return {};

    const float viewCenterZ = cullCameraZAxis_.DotProduct(center) + cullCameraViewMatrix_.m23_;
    const float viewEdgeZ = cullCameraZAxisAbs_.DotProduct(edge);
    const float minZ = viewCenterZ - viewEdgeZ;
    const float maxZ = viewCenterZ + viewEdgeZ;

    return { minZ, maxZ };
}

}
