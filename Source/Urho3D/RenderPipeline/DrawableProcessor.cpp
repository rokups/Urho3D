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
#include "../Graphics/Octree.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/Zone.h"
#include "../IO/Log.h"
#include "../RenderPipeline/RenderPipeline.h"
#include "../RenderPipeline/DrawableProcessor.h"
#include "../RenderPipeline/LightProcessor.h"
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
float GetDrawableLightPenalty(float intensityPenalty, LightImportance importance, LightType type)
{
    switch (importance)
    {
    case LI_IMPORTANT:
        return type == LIGHT_DIRECTIONAL ? -2 : -1;
    case LI_AUTO:
        return intensityPenalty <= 1.0f ? intensityPenalty : 2.0f - 1.0f / intensityPenalty;
    case LI_NOT_IMPORTANT:
        return intensityPenalty <= 1.0f ? 3.0f + intensityPenalty : 5.0f - 1.0f / intensityPenalty;
    default:
        assert(0);
        return M_LARGE_VALUE;
    }
}

}

DrawableProcessorPass::DrawableProcessorPass(RenderPipeline* renderPipeline, bool needAmbient,
    unsigned unlitBasePassIndex, unsigned litBasePassIndex, unsigned lightPassIndex)
    : Object(renderPipeline->GetContext())
    , needAmbient_(needAmbient)
    , unlitBasePassIndex_(unlitBasePassIndex)
    , litBasePassIndex_(litBasePassIndex)
    , lightPassIndex_(lightPassIndex)
{
    renderPipeline->OnUpdateBegin.Subscribe(this, &DrawableProcessorPass::OnUpdateBegin);
}

DrawableProcessorPass::AddResult DrawableProcessorPass::AddBatch(unsigned threadIndex,
    Drawable* drawable, unsigned sourceBatchIndex, Technique* technique)
{
    Pass* unlitBasePass = technique->GetPass(unlitBasePassIndex_);
    Pass* lightPass = technique->GetPass(lightPassIndex_);
    Pass* litBasePass = lightPass ? technique->GetPass(litBasePassIndex_) : nullptr;

    if (!unlitBasePass)
        return { false, false };

    geometryBatches_.PushBack(threadIndex, { drawable, sourceBatchIndex, unlitBasePass, litBasePass, lightPass });
    return { true, !!lightPass };
}

void DrawableProcessorPass::OnUpdateBegin(const FrameInfo& frameInfo)
{
    geometryBatches_.Clear(frameInfo.numThreads_);
}

LightProcessorCache::LightProcessorCache()
{
}

LightProcessorCache::~LightProcessorCache()
{
}

LightProcessor* LightProcessorCache::GetLightProcessor(Light* light)
{
    WeakPtr<Light> weakLight(light);
    auto& lightProcessor = cache_[weakLight];
    if (!lightProcessor)
        lightProcessor = ea::make_unique<LightProcessor>(light);
    return lightProcessor.get();
}

DrawableProcessor::DrawableProcessor(RenderPipeline* renderPipeline)
    : Object(renderPipeline->GetContext())
    , workQueue_(GetSubsystem<WorkQueue>())
    , renderer_(GetSubsystem<Renderer>())
    , defaultMaterial_(renderer_->GetDefaultMaterial())
{
    renderPipeline->OnUpdateBegin.Subscribe(this, &DrawableProcessor::OnUpdateBegin);
}

void DrawableProcessor::SetPasses(ea::vector<SharedPtr<DrawableProcessorPass>> passes)
{
    passes_ = ea::move(passes);
}

void DrawableProcessor::OnUpdateBegin(const FrameInfo& frameInfo)
{
    // Initialize frame constants
    frameInfo_ = frameInfo;
    numDrawables_ = frameInfo_.octree_->GetAllDrawables().size();
    viewMatrix_ = frameInfo_.cullCamera_->GetView();
    viewZ_ = { viewMatrix_.m20_, viewMatrix_.m21_, viewMatrix_.m22_ };
    absViewZ_ = viewZ_.Abs();

    materialQuality_ = renderer_->GetMaterialQuality();
    if (frameInfo_.cullCamera_->GetViewOverrideFlags() & VO_LOW_MATERIAL_QUALITY)
        materialQuality_ = QUALITY_LOW;

    gi_ = frameInfo_.scene_->GetComponent<GlobalIllumination>();

    // Clean temporary containers
    sceneZRangeTemp_.clear();
    sceneZRangeTemp_.resize(frameInfo.numThreads_);
    sceneZRange_ = {};

    isDrawableUpdated_.resize(numDrawables_);
    for (UpdateFlag& isUpdated : isDrawableUpdated_)
        isUpdated.clear(std::memory_order_relaxed);

    geometryFlags_.resize(numDrawables_);
    ea::fill(geometryFlags_.begin(), geometryFlags_.end(), 0);

    geometryZRanges_.resize(numDrawables_);
    geometryLighting_.resize(numDrawables_);

    visibleGeometries_.Clear(frameInfo_.numThreads_);
    threadedGeometryUpdates_.Clear(frameInfo_.numThreads_);
    nonThreadedGeometryUpdates_.Clear(frameInfo_.numThreads_);

    visibleLightsTemp_.Clear(frameInfo_.numThreads_);

    queuedDrawableUpdates_.Clear(frameInfo_.numThreads_);
}

void DrawableProcessor::ProcessVisibleDrawables(const ea::vector<Drawable*>& drawables)
{
    ForEachParallel(workQueue_, drawables,
        [&](unsigned /*index*/, Drawable* drawable)
    {
        ProcessVisibleDrawable(drawable);
    });

    // Sort lights by component ID for stability
    visibleLights_.resize(visibleLightsTemp_.Size());
    ea::copy(visibleLightsTemp_.Begin(), visibleLightsTemp_.End(), visibleLights_.begin());
    const auto compareID = [](Light* lhs, Light* rhs) { return lhs->GetID() < rhs->GetID(); };
    ea::sort(visibleLights_.begin(), visibleLights_.end(), compareID);

    lightProcessors_.clear();
    for (Light* light : visibleLights_)
        lightProcessors_.push_back(lightProcessorCache_.GetLightProcessor(light));

    // Compute scene Z range
    for (const FloatRange& range : sceneZRangeTemp_)
        sceneZRange_ |= range;
}

void DrawableProcessor::UpdateDrawableZone(const BoundingBox& boundingBox, Drawable* drawable)
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
    else
        threadedGeometryUpdates_.PushBack(threadIndex, drawable);
}

void DrawableProcessor::ProcessVisibleDrawable(Drawable* drawable)
{
    // TODO(renderer): Add occlusion culling
    const unsigned drawableIndex = drawable->GetDrawableIndex();
    const unsigned threadIndex = WorkQueue::GetWorkerThreadIndex();

    drawable->UpdateBatches(frameInfo_);
    drawable->MarkInView(frameInfo_);

    isDrawableUpdated_[drawableIndex].test_and_set(std::memory_order_relaxed);

    // Skip if too far
    const float maxDistance = drawable->GetDrawDistance();
    if (maxDistance > 0.0f)
    {
        if (drawable->GetDistance() > maxDistance)
            return;
    }

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
        for (unsigned i = 0; i < sourceBatches.size(); ++i)
        {
            const SourceBatch& sourceBatch = sourceBatches[i];

            // Find current technique
            Material* material = sourceBatch.material_ ? sourceBatch.material_ : defaultMaterial_;
            Technique* technique = material->FindTechnique(drawable, materialQuality_);
            if (!technique)
                continue;

            // Update scene passes
            for (DrawableProcessorPass* pass : passes_)
            {
                const DrawableProcessorPass::AddResult result = pass->AddBatch(threadIndex, drawable, i, technique);
                if (result.litAdded_)
                    isForwardLit = true;
                if (result.litAdded_ || (result.added_ && pass->NeedAmbient()))
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
                lightAccumulator.sh_ = gi_->SampleAmbientSH(samplePosition, hint);
            }
            else
                lightAccumulator.sh_ = {};

            // Apply ambient from Zone
            const CachedDrawableZone& cachedZone = drawable->GetMutableCachedZone();
            lightAccumulator.sh_ += cachedZone.zone_->GetLinearAmbient().ToVector3();
        }

        // Store geometry
        visibleGeometries_.PushBack(threadIndex, drawable);

        // Update flags
        unsigned char& flag = geometryFlags_[drawableIndex];
        flag = GeometryRenderFlag::Visible;
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
            visibleLightsTemp_.PushBack(threadIndex, light);
    }
}

void DrawableProcessor::ProcessForwardLighting(unsigned lightIndex, const ea::vector<Drawable*>& litGeometries)
{
    if (lightIndex >= visibleLights_.size())
    {
        URHO3D_LOGERROR("Invalid light index {}", lightIndex);
        return;
    }

    Light* light = visibleLights_[lightIndex];
    const LightType lightType = light->GetLightType();
    const float lightIntensityPenalty = 1.0f / light->GetIntensityDivisor();

    LightAccumulatorContext ctx;
    ctx.maxVertexLights_ = settings_.maxVertexLights_;
    ctx.maxPixelLights_ = settings_.maxPixelLights_;
    ctx.lightImportance_ = light->GetLightImportance();
    ctx.lightIndex_ = lightIndex;
    ctx.lights_ = &visibleLights_;

    ForEachParallel(workQueue_, litGeometries,
        [&](unsigned /*index*/, Drawable* geometry)
    {
        const float distance = ea::max(light->GetDistanceTo(geometry), M_LARGE_EPSILON);
        const float penalty = GetDrawableLightPenalty(distance * lightIntensityPenalty, ctx.lightImportance_, lightType);
        const unsigned drawableIndex = geometry->GetDrawableIndex();
        geometryLighting_[drawableIndex].AccumulateLight(ctx, penalty);
    });
}

// TODO(renderer): Refactor and reuse me
bool DrawableProcessor::IsShadowCasterVisible(
    Drawable* drawable, BoundingBox lightViewBox, Camera* shadowCamera, const Matrix3x4& lightView,
    const Frustum& lightViewFrustum, const BoundingBox& lightViewFrustumBox) const
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
        if (geometryFlags_[drawableIndex] & GeometryRenderFlag::Visible)
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

void DrawableProcessor::PreprocessShadowCasters(ea::vector<Drawable*>& shadowCasters,
    const ea::vector<Drawable*>& drawables, const FloatRange& zRange, Light* light, Camera* shadowCamera)
{
    const unsigned workerThreadIndex = WorkQueue::GetWorkerThreadIndex();
    unsigned lightMask = light->GetLightMaskEffective();
    Camera* cullCamera = frameInfo_.cullCamera_;

    //Camera* shadowCamera = splits_[splitIndex].shadowCamera_;
    const Frustum& shadowCameraFrustum = shadowCamera->GetFrustum();
    const Matrix3x4& lightView = shadowCamera->GetView();
    const Matrix4& lightProj = shadowCamera->GetProjection();
    LightType type = light->GetLightType();
    const FloatRange& sceneZRange = sceneZRange_;

    //splits_[splitIndex].shadowCasterBox_.Clear();
    shadowCasters.clear();

    // Transform scene frustum into shadow camera's view space for shadow caster visibility check. For point & spot lights,
    // we can use the whole scene frustum. For directional lights, use the intersection of the scene frustum and the split
    // frustum, so that shadow casters do not get rendered into unnecessary splits
    Frustum lightViewFrustum;
    if (type != LIGHT_DIRECTIONAL)
        lightViewFrustum = cullCamera->GetSplitFrustum(sceneZRange.first, sceneZRange.second).Transformed(lightView);
    else
    {
        const FloatRange splitZRange = sceneZRange & zRange;
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
        QueueDrawableUpdate(drawable);

        // Project shadow caster bounding box to light view space for visibility check
        lightViewBox = drawable->GetWorldBoundingBox().Transformed(lightView);

        if (IsShadowCasterVisible(drawable, lightViewBox, shadowCamera, lightView, lightViewFrustum, lightViewFrustumBox))
        {
            // Merge to shadow caster bounding box (only needed for focused spot lights) and add to the list
            if (type == LIGHT_SPOT && light->GetShadowFocus().focus_)
            {
                lightProjBox = lightViewBox.Projected(lightProj);
                //splits_[splitIndex].shadowCasterBox_.Merge(lightProjBox);
            }

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

void DrawableProcessor::ProcessQueuedDrawables()
{
    ForEachParallel(workQueue_, queuedDrawableUpdates_,
        [&](unsigned /*index*/, Drawable* drawable)
    {
        ProcessQueuedDrawable(drawable);
    });
    queuedDrawableUpdates_.Clear(frameInfo_.numThreads_);
}

void DrawableProcessor::ProcessQueuedDrawable(Drawable* drawable)
{
    drawable->UpdateBatches(frameInfo_);
    drawable->MarkInView(frameInfo_);

    const BoundingBox& boundingBox = drawable->GetWorldBoundingBox();
    UpdateDrawableZone(boundingBox, drawable);
    QueueDrawableGeometryUpdate(WorkQueue::GetWorkerThreadIndex(), drawable);
}

void DrawableProcessor::UpdateGeometries()
{
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

    const float viewCenterZ = viewZ_.DotProduct(center) + viewMatrix_.m23_;
    const float viewEdgeZ = absViewZ_.DotProduct(edge);
    const float minZ = viewCenterZ - viewEdgeZ;
    const float maxZ = viewCenterZ + viewEdgeZ;

    return { minZ, maxZ };
}

}
