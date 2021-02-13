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
#include "../Graphics/GlobalIllumination.h"
#include "../Graphics/Octree.h"
#include "../Graphics/OctreeQuery.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/Zone.h"
#include "../RenderPipeline/SceneBatchCollector.h"
#include "../Scene/Scene.h"

#include "../DebugNew.h"

namespace Urho3D
{

SceneBatchCollector::SceneBatchCollector(Context* context, DrawableProcessor* dp, BatchCompositor* bc, LightProcessorCallback* lpc)
    : Object(context)
    , workQueue_(context->GetSubsystem<WorkQueue>())
    , renderer_(context->GetSubsystem<Renderer>())
    , dp_(dp)
    , bc_(bc)
    , lpc_(lpc)
{}

SceneBatchCollector::~SceneBatchCollector()
{
}

/*ea::array<LightProcessor*, SceneBatchCollector::MaxVertexLights> SceneBatchCollector::GetVertexLights(unsigned drawableIndex) const
{
    const auto indices = GetVertexLightIndices(drawableIndex);
    ea::array<LightProcessor*, MaxVertexLights> lights;
    for (unsigned i = 0; i < MaxVertexLights; ++i)
        lights[i] = indices[i] != M_MAX_UNSIGNED ? visibleLights_[indices[i]] : nullptr;
    return lights;
}*/

void SceneBatchCollector::ResetPasses()
{
    passes2_.clear();
}

void SceneBatchCollector::AddScenePass(const SharedPtr<ScenePass>& pass)
{
    passes2_.push_back(pass);
}

void SceneBatchCollector::InvalidatePipelineStateCache()
{
    for (ScenePass* pass : passes2_)
        pass->InvalidatePipelineStateCache();
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

    // Initialize passes
    for (ScenePass* pass : passes2_)
        pass->BeginFrame();
}

void SceneBatchCollector::ProcessVisibleDrawables(const ea::vector<Drawable*>& drawables)
{
    //dp_->ProcessVisibleDrawables(drawables);
    visibleLights_ = dp_->GetLightProcessors();
}

void SceneBatchCollector::ProcessVisibleDrawablesForThread(unsigned threadIndex, ea::span<Drawable* const> drawables)
{
}

void SceneBatchCollector::ProcessVisibleLights()
{
    dp_->ProcessLights(lpc_);
    /*for (LightProcessor* sceneLight : visibleLights_)
        sceneLight->BeginUpdate(dp_, lpc_);

    ForEachParallel(workQueue_, visibleLights_,
        [&](unsigned /index/, LightProcessor* lightProcessor)
    {
        lightProcessor->Update(dp_);
    });

    for (LightProcessor* sceneLight : dp_->GetLightProcessorsSortedByShadowMap())
        sceneLight->EndUpdate(dp_, lpc_);

    // Update lit geometries and shadow casters
    SceneLightProcessContext sceneLightProcessContext;
    sceneLightProcessContext.frameInfo_ = frameInfo_;
    sceneLightProcessContext.dp_ = dp_;
    for (unsigned i = 0; i < visibleLights_.size(); ++i)
    {
        workQueue_->AddWorkItem([this, i, &sceneLightProcessContext](unsigned threadIndex)
        {
            visibleLights_[i]->UpdateLitGeometriesAndShadowCasters(sceneLightProcessContext);
        }, M_MAX_UNSIGNED);
    }
    workQueue_->Complete(M_MAX_UNSIGNED);

    // Finalize scene lights
    for (LightProcessor* sceneLight : visibleLights_)
        sceneLight->FinalizeShadowMap();

    // Sort lights by shadow map size
    const auto compareShadowMapSize = [](const LightProcessor* lhs, const LightProcessor* rhs)
    {
        const IntVector2 lhsSize = lhs->GetShadowMapSize();
        const IntVector2 rhsSize = rhs->GetShadowMapSize();
        if (lhsSize != rhsSize)
            return lhsSize.Length() > rhsSize.Length();
        return lhs->GetLight()->GetID() < rhs->GetLight()->GetID();
    };
    auto visibleLightsSortedBySM = visibleLights_;
    ea::sort(visibleLightsSortedBySM.begin(), visibleLightsSortedBySM.end(), compareShadowMapSize);

    // Assign shadow maps and finalize shadow parameters
    for (LightProcessor* sceneLight : dp_->GetLightProcessorsSortedByShadowMap())
    {
        const IntVector2 shadowMapSize = sceneLight->GetShadowMapSize();
        if (shadowMapSize != IntVector2::ZERO)
        {
            const ShadowMap shadowMap = callback_->GetTemporaryShadowMap(shadowMapSize);
            sceneLight->SetShadowMap(shadowMap);
        }
        sceneLight->FinalizeShaderParameters(camera_, 0.0f);
    }*/

    // Update batches for shadow casters
    //dp_->ProcessShadowCasters();
    bc_->ComposeShadowBatches(dp_->GetLightProcessors());

    // Collect shadow caster batches
    /*for (LightProcessor* sceneLight : visibleLights_)
    {
        const unsigned numSplits = sceneLight->GetNumSplits();
        for (unsigned splitIndex = 0; splitIndex < numSplits; ++splitIndex)
        {
            workQueue_->AddWorkItem([=](unsigned threadIndex)
            {
                bc_->BeginShadowBatchesComposition(visibleLights_.index_of(sceneLight), sceneLight->GetMutableSplit(splitIndex));
            }, M_MAX_UNSIGNED);
        }
    }
    workQueue_->Complete(M_MAX_UNSIGNED);

    // Finalize shadow batches
    bc_->FinalizeShadowBatchesComposition();*/

    // Find main light
    //mainLightIndex_ = FindMainLight();

    // Accumulate lighting
    for (unsigned i = 0; i < visibleLights_.size(); ++i)
        AccumulateForwardLighting(i);
}

/*unsigned SceneBatchCollector::FindMainLight() const
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
}*/

void SceneBatchCollector::AccumulateForwardLighting(unsigned lightIndex)
{
    LightProcessor& sceneLight = *visibleLights_[lightIndex];
    dp_->ProcessForwardLighting(lightIndex, sceneLight.GetLitGeometries());
}

void SceneBatchCollector::CollectSceneBatches()
{
    for (ScenePass* pass : passes2_)
    {
        pass->CollectSceneBatches(0, visibleLights_, dp_, camera_, *callback_);
        //pass->OnBatchesReady();
    }
}

void SceneBatchCollector::UpdateGeometries()
{
    dp_->UpdateGeometries();
}

void SceneBatchCollector::CollectLightVolumeBatches()
{
    lightVolumeBatches_.clear();
    for (unsigned i = 0; i < visibleLights_.size(); ++i)
    {
        LightProcessor* sceneLight = visibleLights_[i];
        Light* light = sceneLight->GetLight();

        LightVolumeBatch batch;
        batch.lightIndex_ = i;
        batch.geometry_ = renderer_->GetLightGeometry(light);
        batch.pipelineState_ = callback_->CreateLightVolumePipelineState(sceneLight, batch.geometry_);
        lightVolumeBatches_.push_back(batch);
    }
}

}
