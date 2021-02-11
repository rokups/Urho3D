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

#include "../IO/Log.h"
#include "../Graphics/Renderer.h"
#include "../RenderPipeline/BatchCompositor.h"
#include "../RenderPipeline/RenderPipeline.h"
/*#include "../Core/Context.h"
#include "../Core/StringUtils.h"
#include "../Core/WorkQueue.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/Technique.h"
#include "../RenderPipeline/ScenePass.h"

#include <EASTL/sort.h>*/

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

/// Batch creation tag.
enum class CreateBatchTag
{
    /// Create batch as is.
    Default,
    /// Override light index to invalid.
    Unlit
};

/// Create batch.
PipelineBatch CreatePipelineBatch(const BatchStateCreateKey& key, PipelineState* pipelineState,
    CreateBatchTag tag = CreateBatchTag::Default)
{
    PipelineBatch batch;
    batch.lightIndex_ = tag == CreateBatchTag::Unlit ? M_MAX_UNSIGNED : key.lightIndex_;
    batch.drawableIndex_ = key.drawable_->GetDrawableIndex();
    batch.sourceBatchIndex_ = key.sourceBatchIndex_;
    batch.geometryType_ = key.geometryType_;
    batch.drawable_ = key.drawable_;
    batch.geometry_ = key.geometry_;
    batch.material_ = key.material_;
    batch.pipelineState_ = pipelineState;
    return batch;
}

/// Add batch or delayed batch.
void AddPipelineBatch(const BatchStateCreateKey& key, BatchStateCache& cache,
    WorkQueueVector<PipelineBatch>& batches, WorkQueueVector<BatchStateCreateKey>& delayedBatches)
{
    PipelineState* pipelineState = cache.GetPipelineState(key);
    if (pipelineState)
        batches.Insert(CreatePipelineBatch(key, pipelineState));
    else
        delayedBatches.Insert(key);
}

}

BatchCompositorPass::BatchCompositorPass(RenderPipeline* renderPipeline,
    const DrawableProcessor* drawableProcessor, bool needAmbient,
    unsigned unlitBasePassIndex, unsigned litBasePassIndex, unsigned lightPassIndex)
    : DrawableProcessorPass(renderPipeline, needAmbient, unlitBasePassIndex, litBasePassIndex, lightPassIndex)
    , workQueue_(GetSubsystem<WorkQueue>())
    , defaultMaterial_(GetSubsystem<Renderer>()->GetDefaultMaterial())
    , drawableProcessor_(drawableProcessor)
    , batchStateCacheCallback_(renderPipeline)
{
    renderPipeline->OnPipelineStatesInvalidated.Subscribe(this, &BatchCompositorPass::OnPipelineStatesInvalidated);
}

void BatchCompositorPass::ComposeBatches()
{
    // Try to process batches in worker threads
    ForEachParallel(workQueue_, geometryBatches_,
        [&](unsigned /*index*/, const GeometryBatch& geometryBatch)
    {
        ProcessGeometryBatch(geometryBatch);
    });

    // Create missing pipeline states from main thread
    ResolveDelayedBatches(UnlitBaseSubpass, delayedUnlitBaseBatches_, unlitBaseCache_, baseBatches_);
    ResolveDelayedBatches(LitBaseSubpass, delayedLitBaseBatches_, litBaseCache_, baseBatches_);
    ResolveDelayedBatches(LightSubpass, delayedLightBatches_, lightCache_, lightBatches_);
}

void BatchCompositorPass::OnUpdateBegin(const FrameInfo& frameInfo)
{
    BaseClassName::OnUpdateBegin(frameInfo);

    baseBatches_.Clear(frameInfo.numThreads_);
    lightBatches_.Clear(frameInfo.numThreads_);

    delayedUnlitBaseBatches_.Clear(frameInfo.numThreads_);
    delayedLitBaseBatches_.Clear(frameInfo.numThreads_);
    delayedLightBatches_.Clear(frameInfo.numThreads_);
}

void BatchCompositorPass::OnPipelineStatesInvalidated()
{
    unlitBaseCache_.Invalidate();
    litBaseCache_.Invalidate();
    lightCache_.Invalidate();
}

bool BatchCompositorPass::InitializeKey(BatchStateCreateKey& key, const GeometryBatch& geometryBatch) const
{
    const auto& sourceBatches = geometryBatch.geometry_->GetBatches();
    if (geometryBatch.sourceBatchIndex_ >= sourceBatches.size())
    {
        // TODO(renderer): Consider removing this check, it should never happen
        URHO3D_LOGERROR("Unexpected source batch index {} for geometry '{}'",
            geometryBatch.sourceBatchIndex_, geometryBatch.geometry_->GetNode()->GetName());
        return false;
    }

    const SourceBatch& sourceBatch = sourceBatches[geometryBatch.sourceBatchIndex_];

    key.drawableHash_ = geometryBatch.geometry_->GetPipelineStateHash();
    key.lightHash_ = 0;
    key.geometryType_ = sourceBatch.geometryType_;
    key.geometry_ = sourceBatch.geometry_;
    key.material_ = sourceBatch.material_ ? sourceBatch.material_.Get() : defaultMaterial_;
    key.pass_ = nullptr;

    key.drawable_ = geometryBatch.geometry_;
    key.sourceBatchIndex_ = geometryBatch.sourceBatchIndex_;
    key.light_ = nullptr;
    key.lightIndex_ = M_MAX_UNSIGNED;
    return true;
}

void BatchCompositorPass::InitializeKeyLight(BatchStateCreateKey& key, unsigned lightIndex) const
{
    key.light_ = drawableProcessor_->GetLightProcessor(lightIndex);
    key.lightHash_ = key.light_->GetPipelineStateHash();
    key.lightIndex_ = lightIndex;
}

void BatchCompositorPass::ProcessGeometryBatch(const GeometryBatch& geometryBatch)
{
    BatchStateCreateKey key;
    if (!InitializeKey(key, geometryBatch))
        return;

    // Process forward lighting if applicable
    bool hasLitBase = false;
    if (geometryBatch.lightPass_)
    {
        const unsigned drawableIndex = key.drawable_->GetDrawableIndex();
        const LightAccumulator& lightAccumulator = drawableProcessor_->GetGeometryLighting(drawableIndex);
        const auto pixelLights = lightAccumulator.GetPixelLights();

        // Add lit base batch if supported and first light is directional (to reduce shader permutations).
        if (geometryBatch.litBasePass_ && !pixelLights.empty())
        {
            const unsigned firstLightIndex = pixelLights[0].second;
            // TODO(renderer): Make this check optional
            const Light* firstLight = drawableProcessor_->GetLight(firstLightIndex);
            if (firstLight->GetLightType() == LIGHT_DIRECTIONAL)
            {
                hasLitBase = true;

                key.pass_ = geometryBatch.litBasePass_;
                InitializeKeyLight(key, firstLightIndex);
                AddPipelineBatch(key, litBaseCache_, baseBatches_, delayedLitBaseBatches_);
            }
        }

        // Add light batches
        key.pass_ = geometryBatch.lightPass_;
        for (unsigned i = hasLitBase ? 1 : 0; i < pixelLights.size(); ++i)
        {
            const unsigned lightIndex = pixelLights[i].second;
            InitializeKeyLight(key, lightIndex);
            AddPipelineBatch(key, lightCache_, lightBatches_, delayedLightBatches_);
        }
    }

    // Add base pass if needed
    if (!hasLitBase)
    {
        key.lightHash_ = 0;
        key.light_ = nullptr;
        key.lightIndex_ = M_MAX_UNSIGNED;

        key.pass_ = geometryBatch.unlitBasePass_;
        AddPipelineBatch(key, unlitBaseCache_, baseBatches_, delayedUnlitBaseBatches_);
    }
}

void BatchCompositorPass::ResolveDelayedBatches(unsigned index, const WorkQueueVector<BatchStateCreateKey>& delayedBatches,
    BatchStateCache& cache, WorkQueueVector<PipelineBatch>& batches)
{
    BatchStateCreateContext ctx;
    ctx.pass_ = this;
    ctx.subpassIndex_ = index;

    for (const BatchStateCreateKey& key : delayedBatches)
    {
        PipelineState* pipelineState = cache.GetOrCreatePipelineState(key, ctx, *batchStateCacheCallback_);
        if (pipelineState)
            batches.Insert(CreatePipelineBatch(key, pipelineState));
    }
}

BatchCompositor::BatchCompositor(RenderPipeline* renderPipeline, const DrawableProcessor* drawableProcessor,
    unsigned shadowPassIndex)
    : Object(renderPipeline->GetContext())
    , shadowPassIndex_(shadowPassIndex)
    , drawableProcessor_(drawableProcessor)
    , defaultMaterial_(GetSubsystem<Renderer>()->GetDefaultMaterial())
    , batchStateCacheCallback_(renderPipeline)
{
    renderPipeline->OnUpdateBegin.Subscribe(this, &BatchCompositor::OnUpdateBegin);
}

void BatchCompositor::SetPasses(ea::vector<SharedPtr<BatchCompositorPass>> passes)
{
    passes_ = passes;
}

void BatchCompositor::ComposeBatches()
{
    for (BatchCompositorPass* pass : passes_)
        pass->ComposeBatches();
}

void BatchCompositor::OnUpdateBegin(const FrameInfo& frameInfo)
{
    materialQuality_ = GetSubsystem<Renderer>()->GetMaterialQuality();
    delayedShadowBatches_.Clear(frameInfo.numThreads_);
}

void BatchCompositor::OnPipelineStatesInvalidated()
{
    shadowCache_.Invalidate();
}

void BatchCompositor::BeginShadowBatchesComposition(unsigned lightIndex, unsigned splitIndex)
{
    LightProcessor* lightProcessor = drawableProcessor_->GetLightProcessor(lightIndex);
    ShadowSplitProcessor& split = lightProcessor->GetMutableSplit(splitIndex);
    const unsigned threadIndex = WorkQueue::GetWorkerThreadIndex();
    const auto& shadowCasters = lightProcessor->GetShadowCasters(splitIndex);
    auto& shadowBatches = lightProcessor->GetMutableShadowBatches(splitIndex);
    const unsigned lightMask = lightProcessor->GetLight()->GetLightMask();
    const unsigned lightHash = lightProcessor->GetPipelineStateHash();
    for (Drawable* drawable : shadowCasters)
    {
        // Check shadow mask now when zone is ready
        if (drawable->GetShadowMaskInZone() & lightMask == 0)
            continue;

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
            Material* material = sourceBatch.material_ ? sourceBatch.material_ : defaultMaterial_;
            Technique* tech = material->FindTechnique(drawable, materialQuality_);
            Pass* pass = tech->GetSupportedPass(shadowPassIndex_);
            if (!pass)
                continue;

            BatchStateCreateKey key;
            key.drawableHash_ = drawable->GetPipelineStateHash();
            key.lightHash_ = lightHash;
            key.geometryType_ = sourceBatch.geometryType_;
            key.geometry_ = sourceBatch.geometry_;
            key.material_ = sourceBatch.material_ ? sourceBatch.material_.Get() : defaultMaterial_;
            key.pass_ = pass;

            key.drawable_ = drawable;
            key.sourceBatchIndex_ = j;
            key.light_ = lightProcessor;
            key.lightIndex_ = lightIndex;

            PipelineState* pipelineState = shadowCache_.GetPipelineState(key);
            if (pipelineState)
                shadowBatches.push_back(CreatePipelineBatch(key, pipelineState, CreateBatchTag::Unlit));
            else
                delayedShadowBatches_.Insert({ &split, key });
        }
    }
}

void BatchCompositor::FinalizeShadowBatchesComposition()
{
    BatchStateCreateContext ctx;
    ctx.pass_ = this;
    ctx.subpassIndex_ = 0;

    for (const auto& splitAndKey : delayedShadowBatches_)
    {
        const BatchStateCreateKey& key = splitAndKey.second;
        ShadowSplitProcessor& split = *splitAndKey.first;
        PipelineState* pipelineState = shadowCache_.GetOrCreatePipelineState(key, ctx, *batchStateCacheCallback_);
        if (pipelineState)
            split.shadowCasterBatches_.push_back(CreatePipelineBatch(key, pipelineState, CreateBatchTag::Unlit));
    }
}

}
