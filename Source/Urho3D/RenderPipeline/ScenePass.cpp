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
#include "../Graphics/Technique.h"
#include "../RenderPipeline/ScenePass.h"

#include <EASTL/sort.h>

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

ea::string NormalizeShaderDefine(ea::string_view define)
{
    if (!define.empty() && define.back() != ' ')
        return ea::string(define) + " ";
    else
        return ea::string(define);
}

}

ScenePass::ScenePass(RenderPipelineInterface* renderPipeline, const DrawableProcessor* drawableProcessor,
    const ea::string& unlitBaseTag, const ea::string& litBaseTag, const ea::string& lightTag,
    unsigned unlitBasePassIndex, unsigned litBasePassIndex, unsigned lightPassIndex)
    : BatchCompositorPass(renderPipeline, drawableProcessor, true, unlitBasePassIndex, litBasePassIndex, lightPassIndex)
    , workQueue_(context_->GetSubsystem<WorkQueue>())
    , renderer_(context_->GetSubsystem<Renderer>())
    /*, unlitBasePassIndex_(unlitBasePassIndex)
    , litBasePassIndex_(litBasePassIndex)
    , lightPassIndex_(lightPassIndex)*/
    , unlitBaseTag_(NormalizeShaderDefine(unlitBaseTag))
    , litBaseTag_(NormalizeShaderDefine(litBaseTag))
    , lightTag_(NormalizeShaderDefine(lightTag))
{
}

void ScenePass::InvalidatePipelineStateCache()
{
    //unlitBasePipelineStateCache_.Invalidate();
    //litBasePipelineStateCache_.Invalidate();
    //lightPipelineStateCache_.Invalidate();
}

void ScenePass::BeginFrame()
{
    numThreads_ = workQueue_->GetNumThreads() + 1;

    //unlitBatches_.Clear(numThreads_);
    //litBatches_.Clear(numThreads_);

    //unlitBaseBatchesDirty_.Clear(numThreads_);
    //litBaseBatchesDirty_.Clear(numThreads_);
    //lightBatchesDirty_.Clear(numThreads_);

    //unlitBaseBatches_.clear();
    //litBaseBatches_.clear();
    //lightBatches_.Clear(numThreads_);
}

/*bool ScenePass::AddSourceBatch(Drawable* drawable, unsigned sourceBatchIndex, Technique* technique)
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
            litBatches_.PushBack(workerThreadIndex, { drawable, sourceBatchIndex, litBasePass, lightPass });
            return true;
        }
        else if (unlitBasePass)
        {
            // Add both unlit and lit batches if there's no lit base
            unlitBatches_.PushBack(workerThreadIndex, { drawable, sourceBatchIndex, unlitBasePass, nullptr });
            litBatches_.PushBack(workerThreadIndex, { drawable, sourceBatchIndex, nullptr, lightPass });
            return true;
        }
    }
    else if (unlitBasePass)
    {
        unlitBatches_.PushBack(workerThreadIndex, { drawable, sourceBatchIndex, unlitBasePass, nullptr });
        return false;
    }
    return false;
}*/

void ScenePass::CollectSceneBatches(unsigned mainLightIndex, ea::span<LightProcessor*> sceneLights,
    const DrawableProcessor* drawableProcessor, Camera* camera, ScenePipelineStateCacheCallback& callback)
{
    ComposeBatches();
    //CollectUnlitBatches(camera, callback);
    //CollectLitBatches(camera, callback, mainLightIndex, sceneLights, drawableProcessor);
}

void ScenePass::CollectUnlitBatches(Camera* camera, ScenePipelineStateCacheCallback& callback)
{
#if 0
    unlitBaseBatches_.resize(unlitBatches_.Size());
    ForEachParallel(workQueue_, BatchThreshold, unlitBatches_,
        [&](unsigned index, const IntermediateSceneBatch& intermediateBatch)
    {
        Material* defaultMaterial = renderer_->GetDefaultMaterial();
        PipelineBatch& sceneBatch = unlitBaseBatches_[index];

        // Add base batch
        sceneBatch = PipelineBatch{ M_MAX_UNSIGNED, intermediateBatch, defaultMaterial };
        sceneBatch.pipelineState_ = unlitBasePipelineStateCache_.GetPipelineState({ sceneBatch, 0 });
        if (!sceneBatch.pipelineState_)
            unlitBaseBatchesDirty_.Insert(&sceneBatch);
    });

    ScenePipelineStateContext subPassContext;
    subPassContext.shaderDefines_ = unlitBaseTag_;
    subPassContext.camera_ = camera;
    subPassContext.light_ = nullptr;

    for (PipelineBatch* sceneBatch : unlitBaseBatchesDirty_)
    {
        const ScenePipelineStateKey key{ *sceneBatch, 0 };
        subPassContext.drawable_ = sceneBatch->drawable_;
        sceneBatch->pipelineState_ = unlitBasePipelineStateCache_.GetOrCreatePipelineState(key, subPassContext, callback);
    };
#endif
}

void ScenePass::CollectLitBatches(Camera* camera, ScenePipelineStateCacheCallback& callback,
    unsigned mainLightIndex, ea::span<LightProcessor*> sceneLights, const DrawableProcessor* drawableProcessor)
{
#if 0
    litBaseBatches_.resize(litBatches_.Size());

    const unsigned mainLightHash = mainLightIndex != M_MAX_UNSIGNED
        ? sceneLights[mainLightIndex]->GetPipelineStateHash()
        : 0;

    ForEachParallel(workQueue_, BatchThreshold, litBatches_,
        [&](unsigned index, const IntermediateSceneBatch& intermediateBatch)
    {
        Material* defaultMaterial = renderer_->GetDefaultMaterial();
        PipelineBatch& sceneBatch = litBaseBatches_[index];

        // TODO(renderer): Optimize lookup
        const auto drawableIndex = intermediateBatch.geometry_->GetDrawableIndex();
        const auto& lighting = drawableProcessor->GetGeometryLighting(drawableIndex);
        const auto pixelLights = lighting.GetPixelLights();
        const bool hasLitBase = !pixelLights.empty() && pixelLights[0].second == mainLightIndex;
        const unsigned baseLightIndex = hasLitBase ? mainLightIndex : M_MAX_UNSIGNED;
        const unsigned baseLightHash = hasLitBase ? mainLightHash : 0;

        // Add base batch
        sceneBatch = PipelineBatch{ baseLightIndex, intermediateBatch, defaultMaterial };
        sceneBatch.pipelineState_ = litBasePipelineStateCache_.GetPipelineState({ sceneBatch, baseLightHash });
        if (!sceneBatch.pipelineState_)
            litBaseBatchesDirty_.Insert(&sceneBatch);

        for (unsigned j = hasLitBase ? 1 : 0; j < pixelLights.size(); ++j)
        {
            const unsigned lightIndex = pixelLights[j].second;
            const unsigned lightHash = sceneLights[lightIndex]->GetPipelineStateHash();

            PipelineBatch lightBatch = sceneBatch;
            lightBatch.lightIndex_ = lightIndex;
            lightBatch.pass_ = intermediateBatch.lightPass_;

            lightBatch.pipelineState_ = lightPipelineStateCache_.GetPipelineState({ lightBatch, lightHash });
            const unsigned batchIndex = lightBatches_.Insert(lightBatch).second;
            if (!lightBatch.pipelineState_)
                lightBatchesDirty_.Insert(batchIndex);
        }
    });

    // Resolve base pipeline states
    {
        ScenePipelineStateContext baseSubPassContext;
        baseSubPassContext.shaderDefines_ = litBaseTag_;
        baseSubPassContext.camera_ = camera;
        baseSubPassContext.litBasePass_ = true;

        for (PipelineBatch* sceneBatch : litBaseBatchesDirty_)
        {
            const LightProcessor* sceneLight = sceneBatch->lightIndex_ != M_MAX_UNSIGNED ? sceneLights[sceneBatch->lightIndex_] : nullptr;
            baseSubPassContext.light_ = sceneLight;
            const unsigned baseLightHash = sceneLight ? sceneLight->GetPipelineStateHash() : 0;

            baseSubPassContext.drawable_ = sceneBatch->drawable_;
            const ScenePipelineStateKey baseKey{ *sceneBatch, baseLightHash };
            sceneBatch->pipelineState_ = litBasePipelineStateCache_.GetOrCreatePipelineState(
                baseKey, baseSubPassContext, callback);
        }
    }

    // Resolve light pipeline states
    {
        ScenePipelineStateContext lightSubPassContext;
        lightSubPassContext.shaderDefines_ = lightTag_;
        lightSubPassContext.camera_ = camera;

        const auto& outerVector = lightBatchesDirty_.GetUnderlyingCollection();
        for (unsigned threadIndex = 0; threadIndex < outerVector.size(); ++threadIndex)
        {
            for (unsigned batchIndex : outerVector[threadIndex])
            {
                PipelineBatch& lightBatch = lightBatches_[{ threadIndex, batchIndex }];
                const LightProcessor* sceneLight = sceneLights[lightBatch.lightIndex_];
                lightSubPassContext.light_ = sceneLight;
                lightSubPassContext.drawable_ = lightBatch.drawable_;

                const ScenePipelineStateKey lightKey{ lightBatch, sceneLight->GetPipelineStateHash() };
                lightBatch.pipelineState_ = lightPipelineStateCache_.GetOrCreatePipelineState(
                    lightKey, lightSubPassContext, callback);
            };
        }
    }
#endif
}

ForwardLightingScenePass::ForwardLightingScenePass(RenderPipelineInterface* renderPipeline, const DrawableProcessor* drawableProcessor, const ea::string& tag,
    const ea::string& unlitBasePass, const ea::string& litBasePass, const ea::string& lightPass)
    : ScenePass(renderPipeline, drawableProcessor,
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
    //SortBatches(unlitBaseBatches_, sortedUnlitBaseBatches_);
    SortBatches(baseBatches_, sortedLitBaseBatches_);
    SortBatches(lightBatches_, sortedLightBatches_);
}

void AlphaForwardLightingScenePass::SortSceneBatches()
{
    //const unsigned numUnlitBaseBatches = unlitBaseBatches_.Size();
    const unsigned numLitBaseBatches = baseBatches_.Size();
    const unsigned numLightBatches = lightBatches_.Size();

    sortedBatches_.resize(/*numUnlitBaseBatches + */numLitBaseBatches + numLightBatches);
    unsigned destIndex = 0;
    //for (const PipelineBatch& batch : unlitBaseBatches_)
    //    sortedBatches_[destIndex++] = PipelineBatchBackToFront{ &batch };
    for (const PipelineBatch& batch : baseBatches_)
        sortedBatches_[destIndex++] = PipelineBatchBackToFront{ &batch };
    for (const PipelineBatch& batch : lightBatches_)
        sortedBatches_[destIndex++] = PipelineBatchBackToFront{ &batch };

    ea::sort(sortedBatches_.begin(), sortedBatches_.end());
}

UnlitScenePass::UnlitScenePass(RenderPipelineInterface* renderPipeline, const DrawableProcessor* drawableProcessor, const ea::string& tag, const ea::string& pass)
    : ScenePass(renderPipeline, drawableProcessor, tag, "", "", Technique::GetPassIndex(pass), M_MAX_UNSIGNED, M_MAX_UNSIGNED)
{
}

void UnlitScenePass::SortSceneBatches()
{
    SortBatches(baseBatches_, sortedBatches_);
}

}
