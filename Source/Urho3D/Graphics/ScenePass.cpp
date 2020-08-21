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
#include "../Core/StringUtils.h"
#include "../Core/WorkQueue.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/ScenePass.h"
#include "../Graphics/Technique.h"

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

ea::string NormalizeShaderDefine(ea::string_view define)
{
    if (define.back() != ' ')
        return ea::string(define) + " ";
    else
        return ea::string(define);
}

}

ScenePass::ScenePass(Context* context,
    const ea::string& unlitBaseTag, const ea::string& litBaseTag, const ea::string& lightTag,
    unsigned unlitBasePassIndex, unsigned litBasePassIndex, unsigned lightPassIndex)
    : Object(context)
    , workQueue_(context_->GetWorkQueue())
    , renderer_(context_->GetRenderer())
    , unlitBasePassIndex_(unlitBasePassIndex)
    , litBasePassIndex_(litBasePassIndex)
    , lightPassIndex_(lightPassIndex)
    , unlitBaseTag_(NormalizeShaderDefine(unlitBaseTag))
    , litBaseTag_(NormalizeShaderDefine(litBaseTag))
    , lightTag_(NormalizeShaderDefine(lightTag))
{
}

void ScenePass::BeginFrame()
{
    numThreads_ = workQueue_->GetNumThreads() + 1;

    unlitBatches_.Clear(numThreads_);
    litBatches_.Clear(numThreads_);

    unlitBaseBatchesDirty_.Clear(numThreads_);
    litBaseBatchesDirty_.Clear(numThreads_);
    lightBatchesDirty_.Clear(numThreads_);

    unlitBaseBatches_.clear();
    litBaseBatches_.clear();
    lightBatches_.Clear(numThreads_);
}

bool ScenePass::AddSourceBatch(Drawable* drawable, unsigned sourceBatchIndex, Technique* technique)
{
    const unsigned workerThreadIndex = WorkQueue::GetWorkerThreadIndex();

    Pass* unlitBasePass = technique->GetPass(unlitBasePassIndex_);
    Pass* litBasePass = technique->GetPass(litBasePassIndex_);
    Pass* lightPass = technique->GetPass(lightPassIndex_);

    if (lightPass)
    {
        if (litBasePass)
        {
            // Add normal lit batch
            litBatches_.Insert(workerThreadIndex, { drawable, sourceBatchIndex, litBasePass, lightPass });
            return true;
        }
        else if (unlitBasePass)
        {
            // Add both unlit and lit batches if there's no lit base
            unlitBatches_.Insert(workerThreadIndex, { drawable, sourceBatchIndex, unlitBasePass, nullptr });
            litBatches_.Insert(workerThreadIndex, { drawable, sourceBatchIndex, nullptr, lightPass });
            return true;
        }
    }
    else if (unlitBasePass)
    {
        unlitBatches_.Insert(workerThreadIndex, { drawable, sourceBatchIndex, unlitBasePass, nullptr });
        return false;
    }
    return false;
}

void ScenePass::CollectSceneBatches(unsigned mainLightIndex, ea::span<SceneLight*> sceneLights,
    const DrawableLightingData& drawableLighting, Camera* camera, ScenePipelineStateCacheCallback& callback)
{
    CollectUnlitBatches(camera, callback);
    CollectLitBatches(camera, callback, mainLightIndex, sceneLights, drawableLighting);
}

void ScenePass::CollectUnlitBatches(Camera* camera, ScenePipelineStateCacheCallback& callback)
{
    unlitBaseBatches_.resize(unlitBatches_.Size());
    ForEachParallel(workQueue_, BatchThreshold, unlitBatches_,
        [&](unsigned threadIndex, unsigned offset, ea::span<IntermediateSceneBatch const> batches)
    {
        Material* defaultMaterial = renderer_->GetDefaultMaterial();
        for (unsigned i = 0; i < batches.size(); ++i)
        {
            const IntermediateSceneBatch& intermediateBatch = batches[i];
            BaseSceneBatch& sceneBatch = unlitBaseBatches_[i + offset];

            // Add base batch
            sceneBatch = BaseSceneBatch{ M_MAX_UNSIGNED, intermediateBatch, defaultMaterial };
            sceneBatch.pipelineState_ = unlitPipelineStateCache_.GetPipelineState({ sceneBatch, 0 });
            if (!sceneBatch.pipelineState_)
                unlitBaseBatchesDirty_.Insert(threadIndex, &sceneBatch);
        }
    });

    ScenePipelineStateContext subPassContext;
    subPassContext.shaderDefines_ = unlitBaseTag_;
    subPassContext.camera_ = camera;
    subPassContext.light_ = nullptr;

    unlitBaseBatchesDirty_.ForEach([&](unsigned, unsigned, BaseSceneBatch* sceneBatch)
    {
        const ScenePipelineStateKey key{ *sceneBatch, 0 };
        subPassContext.drawable_ = sceneBatch->drawable_;
        sceneBatch->pipelineState_ = unlitPipelineStateCache_.GetOrCreatePipelineState(key, subPassContext, callback);
    });
}

void ScenePass::CollectLitBatches(Camera* camera, ScenePipelineStateCacheCallback& callback,
    unsigned mainLightIndex, ea::span<SceneLight*> sceneLights, const DrawableLightingData& drawableLighting)
{
    litBaseBatches_.resize(litBatches_.Size());

    const unsigned mainLightHash = mainLightIndex != M_MAX_UNSIGNED
        ? sceneLights[mainLightIndex]->GetPipelineStateHash()
        : 0;

    ForEachParallel(workQueue_, BatchThreshold, litBatches_,
        [&](unsigned threadIndex, unsigned offset, ea::span<IntermediateSceneBatch const> batches)
    {
        Material* defaultMaterial = renderer_->GetDefaultMaterial();
        for (unsigned i = 0; i < batches.size(); ++i)
        {
            const IntermediateSceneBatch& intermediateBatch = batches[i];
            BaseSceneBatch& sceneBatch = litBaseBatches_[i + offset];

            const auto pixelLights = drawableLighting[sceneBatch.drawableIndex_].GetPixelLights();
            const bool hasLitBase = !pixelLights.empty() && pixelLights[0].second == mainLightIndex;
            const unsigned baseLightIndex = hasLitBase ? mainLightIndex : M_MAX_UNSIGNED;
            const unsigned baseLightHash = hasLitBase ? mainLightHash : 0;

            // Add base batch
            sceneBatch = BaseSceneBatch{ baseLightIndex, intermediateBatch, defaultMaterial };
            sceneBatch.pipelineState_ = litPipelineStateCache_.GetPipelineState({ sceneBatch, baseLightHash });
            if (!sceneBatch.pipelineState_)
                litBaseBatchesDirty_.Insert(threadIndex, &sceneBatch);

            for (unsigned j = hasLitBase ? 1 : 0; j < pixelLights.size(); ++j)
            {
                const unsigned lightIndex = pixelLights[j].second;
                const unsigned lightHash = sceneLights[lightIndex]->GetPipelineStateHash();

                BaseSceneBatch lightBatch = sceneBatch;
                lightBatch.lightIndex_ = lightIndex;
                lightBatch.pass_ = intermediateBatch.additionalPass_;

                lightBatch.pipelineState_ = additionalLightPipelineStateCache_.GetPipelineState({ lightBatch, lightHash });
                const unsigned batchIndex = lightBatches_.Insert(threadIndex, lightBatch);
                if (!lightBatch.pipelineState_)
                    lightBatchesDirty_.Insert(threadIndex, batchIndex);
            }
        }
    });

    // Resolve base pipeline states
    {
        ScenePipelineStateContext baseSubPassContext;
        baseSubPassContext.shaderDefines_ = litBaseTag_;
        baseSubPassContext.camera_ = camera;
        baseSubPassContext.light_ = mainLightIndex != M_MAX_UNSIGNED ? sceneLights[mainLightIndex] : nullptr;
        const unsigned baseLightHash = mainLightIndex != M_MAX_UNSIGNED ? mainLightHash : 0;

        litBaseBatchesDirty_.ForEach([&](unsigned, unsigned, BaseSceneBatch* sceneBatch)
        {
            baseSubPassContext.drawable_ = sceneBatch->drawable_;
            const ScenePipelineStateKey baseKey{ *sceneBatch, baseLightHash };
            sceneBatch->pipelineState_ = litPipelineStateCache_.GetOrCreatePipelineState(
                baseKey, baseSubPassContext, callback);
        });
    }

    // Resolve light pipeline states
    {
        ScenePipelineStateContext lightSubPassContext;
        lightSubPassContext.shaderDefines_ = lightTag_;
        lightSubPassContext.camera_ = camera;

        lightBatchesDirty_.ForEach([&](unsigned threadIndex, unsigned, unsigned batchIndex)
        {
            BaseSceneBatch& lightBatch = lightBatches_.Get(threadIndex, batchIndex);
            const SceneLight* sceneLight = sceneLights[lightBatch.lightIndex_];
            lightSubPassContext.light_ = sceneLight;
            lightSubPassContext.drawable_ = lightBatch.drawable_;

            const ScenePipelineStateKey lightKey{ lightBatch, sceneLight->GetPipelineStateHash() };
            lightBatch.pipelineState_ = additionalLightPipelineStateCache_.GetOrCreatePipelineState(
                lightKey, lightSubPassContext, callback);
        });
    }
}

ForwardLightingScenePass::ForwardLightingScenePass(Context* context, const ea::string& tag,
    const ea::string& unlitBasePass, const ea::string& litBasePass, const ea::string& lightPass)
    : ScenePass(context,
        Format("{0} {0}_UNLIT", tag),
        Format("{0} {0}_LITBASE", tag),
        Format("{0} {0}_LIGHT", tag),
        Technique::GetPassIndex(unlitBasePass),
        Technique::GetPassIndex(litBasePass),
        Technique::GetPassIndex(lightPass))
{
    assert(!unlitBasePass.empty());
    assert(!litBasePass.empty());
    assert(!lightPass.empty());
}

void OpaqueForwardLightingScenePass::SortSceneBatches()
{
    SortBatches(unlitBaseBatches_, sortedUnlitBaseBatches_);
    SortBatches(litBaseBatches_, sortedLitBaseBatches_);
    SortBatches(lightBatches_, sortedLightBatches_);
}

void AlphaForwardLightingScenePass::SortSceneBatches()
{
    const unsigned numUnlitBaseBatches = unlitBaseBatches_.size();
    const unsigned numLitBaseBatches = litBaseBatches_.size();
    const unsigned numLightBatches = lightBatches_.Size();

    sortedBatches_.resize(numUnlitBaseBatches + numLitBaseBatches + numLightBatches);
    unsigned destIndex = 0;
    for (unsigned i = 0; i < numUnlitBaseBatches; ++i)
        sortedBatches_[destIndex++] = BaseSceneBatchSortedBackToFront{ &unlitBaseBatches_[i] };
    for (unsigned i = 0; i < numLitBaseBatches; ++i)
        sortedBatches_[destIndex++] = BaseSceneBatchSortedBackToFront{ &litBaseBatches_[i] };
    lightBatches_.ForEach([&](unsigned, unsigned, const BaseSceneBatch& batch)
    {
        sortedBatches_[destIndex++] = BaseSceneBatchSortedBackToFront{ &batch };
    });

    ea::sort(sortedBatches_.begin(), sortedBatches_.end());
}

ShadowScenePass::ShadowScenePass(Context* context, const ea::string& tag, const ea::string& shadowPass)
    : Object(context)
    , workQueue_(context_->GetWorkQueue())
    , renderer_(context_->GetRenderer())
    , shadowPassIndex_(Technique::GetPassIndex(shadowPass))
    , tag_(NormalizeShaderDefine(tag))
{
}

void ShadowScenePass::BeginFrame()
{
    numThreads_ = workQueue_->GetNumThreads() + 1;
    batchesDirty_.Clear(numThreads_);
}

void ShadowScenePass::CollectShadowBatches(MaterialQuality materialQuality, SceneLight* sceneLight, unsigned splitIndex)
{
    const unsigned threadIndex = WorkQueue::GetWorkerThreadIndex();
    Material* defaultMaterial = renderer_->GetDefaultMaterial();
    const auto& shadowCasters = sceneLight->GetShadowCasters(splitIndex);
    auto& shadowBatches = sceneLight->GetMutableShadowBatches(splitIndex);
    for (Drawable* drawable : shadowCasters)
    {
        // Check shadow distance
        float maxShadowDistance = drawable->GetShadowDistance();
        const float drawDistance = drawable->GetDrawDistance();
        if (drawDistance > 0.0f && (maxShadowDistance <= 0.0f || drawDistance < maxShadowDistance))
            maxShadowDistance = drawDistance;
        if (maxShadowDistance > 0.0f && drawable->GetDistance() > maxShadowDistance)
            continue;

        // Add batches
        const auto& sourceBatches = drawable->GetBatches();
        for (unsigned j = 0; j < sourceBatches.size(); ++j)
        {
            const SourceBatch& sourceBatch = sourceBatches[j];
            Material* material = sourceBatch.material_ ? sourceBatch.material_ : defaultMaterial;
            Technique* tech = material->FindTechnique(drawable, materialQuality);
            Pass* pass = tech->GetSupportedPass(shadowPassIndex_);
            if (!pass)
                continue;

            BaseSceneBatch batch;
            batch.drawableIndex_ = drawable->GetDrawableIndex();
            batch.sourceBatchIndex_ = j;
            batch.geometryType_ = sourceBatch.geometryType_;
            batch.drawable_ = drawable;
            batch.geometry_ = sourceBatch.geometry_;
            batch.material_ = material;
            batch.pass_ = pass;

            const ScenePipelineStateKey key{ batch, sceneLight->GetPipelineStateHash() };
            batch.pipelineState_ = pipelineStateCache_.GetPipelineState(key);
            if (!batch.pipelineState_)
            {
                SceneLightShadowSplit& split = sceneLight->GetMutableSplit(splitIndex);
                batchesDirty_.Insert(threadIndex, { &split, shadowBatches.size() });
            }

            shadowBatches.push_back(batch);
        }
    }
}

void ShadowScenePass::FinalizeShadowBatches(Camera* camera, ScenePipelineStateCacheCallback& callback)
{
    ScenePipelineStateContext subPassContext;
    subPassContext.shaderDefines_ = tag_;
    subPassContext.shadowPass_ = true;
    subPassContext.camera_ = camera;

    batchesDirty_.ForEach([&](unsigned, unsigned, const ea::pair<SceneLightShadowSplit*, unsigned>& splitAndBatch)
    {
        SceneLightShadowSplit& split = *splitAndBatch.first;
        BaseSceneBatch& shadowBatch = split.shadowCasterBatches_[splitAndBatch.second];
        subPassContext.drawable_ = shadowBatch.drawable_;
        subPassContext.light_ = split.sceneLight_;
        const ScenePipelineStateKey baseKey{ shadowBatch, split.sceneLight_->GetPipelineStateHash() };
        shadowBatch.pipelineState_ = pipelineStateCache_.GetOrCreatePipelineState(
            baseKey, subPassContext, callback);
    });
}

ea::span<const BaseSceneBatchSortedByState> ShadowScenePass::GetSortedShadowBatches(const SceneLightShadowSplit& split) const
{
    static thread_local ea::vector<BaseSceneBatchSortedByState> sortedBatchesStorage;
    auto& sortedBatches = sortedBatchesStorage;

    const ea::vector<BaseSceneBatch>& batches = split.shadowCasterBatches_;
    const unsigned numBatches = batches.size();
    sortedBatches.resize(numBatches);
    for (unsigned i = 0; i < numBatches; ++i)
        sortedBatches[i] = BaseSceneBatchSortedByState{ &batches[i] };
    ea::sort(sortedBatches.begin(), sortedBatches.end());
    return sortedBatches;
}

}
