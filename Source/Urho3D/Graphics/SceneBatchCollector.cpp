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
#include "../Graphics/Camera.h"
#include "../Graphics/Geometry.h"
#include "../Graphics/Octree.h"
#include "../Graphics/OctreeQuery.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/SceneBatchCollector.h"
#include "../Scene/Scene.h"

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

/// Helper class to evaluate min and max Z of the drawable.
struct DrawableZRangeEvaluator
{
    explicit DrawableZRangeEvaluator(Camera* camera)
        : viewMatrix_(camera->GetView())
        , viewZ_(viewMatrix_.m20_, viewMatrix_.m21_, viewMatrix_.m22_)
        , absViewZ_(viewZ_.Abs())
    {
    }

    DrawableZRange Evaluate(Drawable* drawable) const
    {
        const BoundingBox& boundingBox = drawable->GetWorldBoundingBox();
        const Vector3 center = boundingBox.Center();
        const Vector3 edge = boundingBox.Size() * 0.5f;

        // Ignore "infinite" objects like skybox
        if (edge.LengthSquared() >= M_LARGE_VALUE * M_LARGE_VALUE)
            return {};

        const float viewCenterZ = viewZ_.DotProduct(center) + viewMatrix_.m23_;
        const float viewEdgeZ = absViewZ_.DotProduct(edge);
        const float minZ = viewCenterZ - viewEdgeZ;
        const float maxZ = viewCenterZ + viewEdgeZ;

        return { minZ, maxZ };
    }

    Matrix3x4 viewMatrix_;
    Vector3 viewZ_;
    Vector3 absViewZ_;
};

}

SceneBatchCollector::SceneBatchCollector(Context* context)
    : Object(context)
    , workQueue_(context->GetWorkQueue())
    , renderer_(context->GetRenderer())
{}

SceneBatchCollector::~SceneBatchCollector()
{
}

ea::array<SceneLight*, SceneBatchCollector::MaxVertexLights> SceneBatchCollector::GetVertexLights(unsigned drawableIndex) const
{
    const auto indices = GetVertexLightIndices(drawableIndex);
    ea::array<SceneLight*, MaxVertexLights> lights;
    for (unsigned i = 0; i < MaxVertexLights; ++i)
        lights[i] = indices[i] != M_MAX_UNSIGNED ? visibleLights_[indices[i]] : nullptr;
    return lights;
}

void SceneBatchCollector::ResetPasses()
{
    passes2_.clear();
}

void SceneBatchCollector::SetShadowPass(const SharedPtr<ShadowScenePass>& shadowPass)
{
    shadowPass_ = shadowPass;
}

void SceneBatchCollector::AddScenePass(const SharedPtr<ScenePass>& pass)
{
    passes2_.push_back(pass);
}

void SceneBatchCollector::BeginFrame(const FrameInfo& frameInfo, SceneBatchCollectorCallback& callback)
{
    // Initialize context
    numThreads_ = workQueue_->GetNumThreads() + 1;
    callback_ = &callback;
    materialQuality_ = renderer_->GetMaterialQuality();

    frameInfo_ = frameInfo;
    octree_ = frameInfo.octree_;
    camera_ = frameInfo.camera_;
    numDrawables_ = octree_->GetAllDrawables().size();

    if (camera_->GetViewOverrideFlags() & VO_LOW_MATERIAL_QUALITY)
        materialQuality_ = QUALITY_LOW;

    // Reset containers
    visibleGeometries_.Clear(numThreads_);
    visibleLightsTemp_.Clear(numThreads_);
    sceneZRange_.Clear(numThreads_);
    shadowCastersToBeUpdated_.Clear(numThreads_);
    threadedGeometryUpdates_.Clear(numThreads_);
    nonThreadedGeometryUpdates_.Clear(numThreads_);

    transient_.Reset(numDrawables_);
    drawableLighting_.resize(numDrawables_);

    // Initialize passes
    if (shadowPass_)
        shadowPass_->BeginFrame();
    for (ScenePass* pass : passes2_)
        pass->BeginFrame();
}

void SceneBatchCollector::ProcessVisibleDrawables(const ea::vector<Drawable*>& drawables)
{
    ForEachParallel(workQueue_, drawableWorkThreshold_, drawables,
        [this](unsigned threadIndex, unsigned /*offset*/, ea::span<Drawable* const> drawablesRange)
    {
        ProcessVisibleDrawablesForThread(threadIndex, drawablesRange);
    });

    visibleLights_.clear();
    visibleLightsTemp_.ForEach([&](unsigned, unsigned, Light* light)
    {
        WeakPtr<Light> weakLight(light);
        auto& sceneLight = cachedSceneLights_[weakLight];
        if (!sceneLight)
            sceneLight = ea::make_unique<SceneLight>(light);
        visibleLights_.push_back(sceneLight.get());
    });
}

void SceneBatchCollector::ProcessVisibleDrawablesForThread(unsigned threadIndex, ea::span<Drawable* const> drawables)
{
    Material* defaultMaterial = renderer_->GetDefaultMaterial();
    const DrawableZRangeEvaluator zRangeEvaluator{ camera_ };

    for (Drawable* drawable : drawables)
    {
        // TODO(renderer): Add occlusion culling
        const unsigned drawableIndex = drawable->GetDrawableIndex();

        drawable->UpdateBatches(frameInfo_);
        drawable->MarkInView(frameInfo_);
        transient_.isUpdated_[drawableIndex].test_and_set(std::memory_order_relaxed);

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
            const DrawableZRange zRange = zRangeEvaluator.Evaluate(drawable);

            // Do not add "infinite" objects like skybox to prevent shadow map focusing behaving erroneously
            if (!zRange.IsValid())
                transient_.zRange_[drawableIndex] = { M_LARGE_VALUE, M_LARGE_VALUE };
            else
            {
                transient_.zRange_[drawableIndex] = zRange;
                sceneZRange_.Accumulate(threadIndex, zRange);
            }

            visibleGeometries_.Insert(threadIndex, drawable);
            transient_.traits_[drawableIndex] |= SceneDrawableData::DrawableVisibleGeometry;

            // Queue geometry update
            const UpdateGeometryType updateGeometryType = drawable->GetUpdateGeometryType();
            if (updateGeometryType == UPDATE_MAIN_THREAD)
                nonThreadedGeometryUpdates_.Insert(threadIndex, drawable);
            else
                threadedGeometryUpdates_.Insert(threadIndex, drawable);

            // Collect batches
            const auto& sourceBatches = drawable->GetBatches();
            for (unsigned i = 0; i < sourceBatches.size(); ++i)
            {
                const SourceBatch& sourceBatch = sourceBatches[i];

                // Find current technique
                Material* material = sourceBatch.material_ ? sourceBatch.material_ : defaultMaterial;
                Technique* technique = material->FindTechnique(drawable, materialQuality_);
                if (!technique)
                    continue;

                // Update scene passes
                for (ScenePass* pass : passes2_)
                {
                    const bool isLit = pass->AddSourceBatch(drawable, i, technique);
                    if (isLit)
                        transient_.traits_[drawableIndex] |= SceneDrawableData::ForwardLit;
                }
            }

            // Reset light accumulator
            // TODO(renderer): Don't do it if unlit
            drawableLighting_[drawableIndex].Reset();
        }
        else if (drawable->GetDrawableFlags() & DRAWABLE_LIGHT)
        {
            auto light = static_cast<Light*>(drawable);
            const Color lightColor = light->GetEffectiveColor();

            // Skip lights with zero brightness or black color, skip baked lights too
            if (!lightColor.Equals(Color::BLACK) && light->GetLightMaskEffective() != 0)
                visibleLightsTemp_.Insert(threadIndex, light);
        }
    }
}

void SceneBatchCollector::ProcessVisibleLights()
{
    // Process lights in main thread
    for (SceneLight* sceneLight : visibleLights_)
        sceneLight->BeginFrame(shadowPass_ && callback_->HasShadow(sceneLight->GetLight()));

    // Update lit geometries and shadow casters
    SceneLightProcessContext sceneLightProcessContext;
    sceneLightProcessContext.frameInfo_ = frameInfo_;
    sceneLightProcessContext.sceneZRange_ = sceneZRange_.Get();
    sceneLightProcessContext.visibleGeometries_ = &visibleGeometries_;
    sceneLightProcessContext.drawableData_ = &transient_;
    sceneLightProcessContext.geometriesToBeUpdates_ = &shadowCastersToBeUpdated_;
    for (unsigned i = 0; i < visibleLights_.size(); ++i)
    {
        workQueue_->AddWorkItem([this, i, &sceneLightProcessContext](unsigned threadIndex)
        {
            visibleLights_[i]->UpdateLitGeometriesAndShadowCasters(sceneLightProcessContext);
        }, M_MAX_UNSIGNED);
    }
    workQueue_->Complete(M_MAX_UNSIGNED);

    // Finalize scene lights
    for (SceneLight* sceneLight : visibleLights_)
        sceneLight->FinalizeShadowMap();

    // Sort lights by shadow map size
    const auto isShadowMapBigger = [](const SceneLight* lhs, const SceneLight* rhs)
    {
        return lhs->GetShadowMapSize().Length() > rhs->GetShadowMapSize().Length();
    };
    ea::sort(visibleLights_.begin(), visibleLights_.end(), isShadowMapBigger);

    // Assign shadow maps and finalize shadow parameters
    for (SceneLight* sceneLight : visibleLights_)
    {
        const IntVector2 shadowMapSize = sceneLight->GetShadowMapSize();
        if (shadowMapSize != IntVector2::ZERO)
        {
            const ShadowMap shadowMap = callback_->GetTemporaryShadowMap(shadowMapSize);
            sceneLight->SetShadowMap(shadowMap);
        }
        sceneLight->FinalizeShaderParameters(camera_, 0.0f);
    }

    // Update batches for shadow casters
    ForEachParallel(workQueue_, 1u, shadowCastersToBeUpdated_,
        [&](unsigned threadIndex, unsigned /*offset*/, ea::span<Drawable* const> geometries)
    {
        for (Drawable* drawable : geometries)
        {
            drawable->UpdateBatches(frameInfo_);
            drawable->MarkInView(frameInfo_);

            // Queue geometry update
            const UpdateGeometryType updateGeometryType = drawable->GetUpdateGeometryType();
            if (updateGeometryType == UPDATE_MAIN_THREAD)
                nonThreadedGeometryUpdates_.Insert(threadIndex, drawable);
            else
                threadedGeometryUpdates_.Insert(threadIndex, drawable);
        }
    });

    // Collect shadow caster batches
    for (SceneLight* sceneLight : visibleLights_)
    {
        const unsigned numSplits = sceneLight->GetNumSplits();
        for (unsigned splitIndex = 0; splitIndex < numSplits; ++splitIndex)
        {
            workQueue_->AddWorkItem([=](unsigned threadIndex)
            {
                shadowPass_->CollectShadowBatches(materialQuality_, sceneLight, splitIndex);
            }, M_MAX_UNSIGNED);
        }
    }
    workQueue_->Complete(M_MAX_UNSIGNED);

    // Finalize shadow batches
    if (shadowPass_)
        shadowPass_->FinalizeShadowBatches(camera_, *callback_);

    // Find main light
    mainLightIndex_ = FindMainLight();

    // Accumulate lighting
    for (unsigned i = 0; i < visibleLights_.size(); ++i)
        AccumulateForwardLighting(i);
}

unsigned SceneBatchCollector::FindMainLight() const
{
    float mainLightScore = 0.0f;
    unsigned mainLightIndex = M_MAX_UNSIGNED;
    for (unsigned i = 0; i < visibleLights_.size(); ++i)
    {
        Light* light = visibleLights_[i]->GetLight();
        if (light->GetLightType() != LIGHT_DIRECTIONAL)
            continue;

        const float score = light->GetIntensityDivisor();
        if (score > mainLightScore)
        {
            mainLightScore = score;
            mainLightIndex = i;
        }
    }
    return mainLightIndex;
}

void SceneBatchCollector::AccumulateForwardLighting(unsigned lightIndex)
{
    SceneLight& sceneLight = *visibleLights_[lightIndex];
    Light* light = sceneLight.GetLight();

    ForEachParallel(workQueue_, litGeometriesWorkThreshold_, sceneLight.GetLitGeometries(),
        [&](unsigned /*threadIndex*/, unsigned /*offset*/, ea::span<Drawable* const> geometries)
    {
        DrawableLightDataAccumulationContext accumContext;
        accumContext.maxPixelLights_ = maxPixelLights_;
        accumContext.lightImportance_ = light->GetLightImportance();
        accumContext.lightIndex_ = lightIndex;
        // TODO(renderer): fixme
        //accumContext.lights_ = &visibleLights_;

        const float lightIntensityPenalty = 1.0f / light->GetIntensityDivisor();

        for (Drawable* geometry : geometries)
        {
            const unsigned drawableIndex = geometry->GetDrawableIndex();
            const float distance = ea::max(light->GetDistanceTo(geometry), M_LARGE_EPSILON);
            const float penalty = lightIndex == mainLightIndex_ ? -M_LARGE_VALUE : distance * lightIntensityPenalty;
            drawableLighting_[drawableIndex].AccumulateLight(accumContext, penalty);
        }
    });
}

void SceneBatchCollector::CollectSceneBatches()
{
    for (ScenePass* pass : passes2_)
    {
        pass->CollectSceneBatches(mainLightIndex_, visibleLights_, drawableLighting_, camera_, *callback_);
        pass->SortSceneBatches();
    }
}

void SceneBatchCollector::UpdateGeometries()
{
    // TODO(renderer): Add multithreading
    threadedGeometryUpdates_.ForEach([&](unsigned, unsigned, Drawable* drawable)
    {
        drawable->UpdateGeometry(frameInfo_);
    });
    nonThreadedGeometryUpdates_.ForEach([&](unsigned, unsigned, Drawable* drawable)
    {
        drawable->UpdateGeometry(frameInfo_);
    });
}

}
