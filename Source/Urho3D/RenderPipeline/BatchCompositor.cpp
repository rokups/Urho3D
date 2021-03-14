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
#include "../RenderPipeline/LightProcessor.h"
#include "../RenderPipeline/RenderPipelineDefs.h"
#include "../Scene/Node.h"

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

/// Create batch. TODO(renderer): Use constexpr if
PipelineBatch CreatePipelineBatch(const BatchStateCreateKey& key,
    PipelineState* pipelineState, CreateBatchTag tag = CreateBatchTag::Default)
{
    PipelineBatch batch;
    batch.pixelLightIndex_ = tag == CreateBatchTag::Unlit ? M_MAX_UNSIGNED : key.pixelLightIndex_;
    batch.vertexLightsHash_ = tag == CreateBatchTag::Unlit ? 0 : key.vertexLightsHash_;
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
    {
        if (pipelineState->IsValid())
            batches.Insert(CreatePipelineBatch(key, pipelineState));
    }
    else
        delayedBatches.Insert(key);
}

}

BatchCompositorPass::BatchCompositorPass(RenderPipelineInterface* renderPipeline,
    DrawableProcessor* drawableProcessor, BatchStateCacheCallback* callback, DrawableProcessorPassFlags flags,
    unsigned overridePassIndex, unsigned unlitBasePassIndex, unsigned litBasePassIndex, unsigned lightPassIndex)
    : DrawableProcessorPass(renderPipeline, flags, overridePassIndex, unlitBasePassIndex, litBasePassIndex, lightPassIndex)
    , workQueue_(GetSubsystem<WorkQueue>())
    , defaultMaterial_(GetSubsystem<Renderer>()->GetDefaultMaterial())
    , drawableProcessor_(drawableProcessor)
    , batchStateCacheCallback_(callback)
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
    ResolveDelayedBatches(OverrideSubpass, delayedOverrideBatches_, overrideCache_, overrideBatches_);
    ResolveDelayedBatches(BaseSubpass, delayedUnlitBaseBatches_, unlitBaseCache_, baseBatches_);
    ResolveDelayedBatches(BaseSubpass, delayedLitBaseBatches_, litBaseCache_, baseBatches_);
    ResolveDelayedBatches(LightSubpass, delayedLightBatches_, lightCache_, lightBatches_);

    OnBatchesReady();
}

void BatchCompositorPass::OnUpdateBegin(const CommonFrameInfo& frameInfo)
{
    BaseClassName::OnUpdateBegin(frameInfo);

    overrideBatches_.Clear();
    baseBatches_.Clear();
    lightBatches_.Clear();

    delayedOverrideBatches_.Clear();
    delayedUnlitBaseBatches_.Clear();
    delayedLitBaseBatches_.Clear();
    delayedLightBatches_.Clear();
}

void BatchCompositorPass::OnPipelineStatesInvalidated()
{
    overrideCache_.Invalidate();
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
    key.pixelLightHash_ = 0;
    key.geometryType_ = sourceBatch.geometryType_;
    key.geometry_ = sourceBatch.geometry_;
    key.material_ = sourceBatch.material_ ? sourceBatch.material_.Get() : defaultMaterial_;
    key.pass_ = nullptr;

    key.drawable_ = geometryBatch.geometry_;
    key.sourceBatchIndex_ = geometryBatch.sourceBatchIndex_;
    key.pixelLight_ = nullptr;
    key.pixelLightIndex_ = M_MAX_UNSIGNED;
    key.vertexLightsHash_ = 0;
    return true;
}

void BatchCompositorPass::InitializeKeyLight(BatchStateCreateKey& key,
    unsigned lightIndex, unsigned vertexLightsHash) const
{
    key.pixelLight_ = drawableProcessor_->GetLightProcessor(lightIndex);
    key.pixelLightHash_ = key.pixelLight_->GetForwardLitHash();
    key.pixelLightIndex_ = lightIndex;
    key.vertexLightsHash_ = vertexLightsHash;
}

void BatchCompositorPass::ProcessGeometryBatch(const GeometryBatch& geometryBatch)
{
    BatchStateCreateKey key;
    if (!InitializeKey(key, geometryBatch))
        return;

    if (geometryBatch.overridePass_)
    {
        key.pass_ = geometryBatch.overridePass_;
        AddPipelineBatch(key, overrideCache_, overrideBatches_, delayedOverrideBatches_);
        return;
    }

    // Process forward lighting if applicable
    bool hasLitBase = false;
    unsigned vertexLightsHash = 0;
    if (geometryBatch.lightPass_)
    {
        const unsigned drawableIndex = key.drawable_->GetDrawableIndex();
        LightAccumulator& lightAccumulator = drawableProcessor_->GetMutableGeometryLighting(drawableIndex);
        lightAccumulator.Cook();

        const auto pixelLights = lightAccumulator.GetPixelLights();
        vertexLightsHash = lightAccumulator.GetVertexLightsHash();

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
                InitializeKeyLight(key, firstLightIndex, vertexLightsHash);
                AddPipelineBatch(key, litBaseCache_, baseBatches_, delayedLitBaseBatches_);
            }
        }

        // Add light batches
        key.pass_ = geometryBatch.lightPass_;
        for (unsigned i = hasLitBase ? 1 : 0; i < pixelLights.size(); ++i)
        {
            const unsigned lightIndex = pixelLights[i].second;
            InitializeKeyLight(key, lightIndex, 0);
            AddPipelineBatch(key, lightCache_, lightBatches_, delayedLightBatches_);
        }
    }

    // Add base pass if needed
    if (!hasLitBase)
    {
        key.pixelLightHash_ = 0;
        key.pixelLight_ = nullptr;
        key.pixelLightIndex_ = M_MAX_UNSIGNED;

        key.pass_ = geometryBatch.unlitBasePass_;
        key.vertexLightsHash_ = vertexLightsHash;
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
        PipelineState* pipelineState = cache.GetOrCreatePipelineState(key, ctx, batchStateCacheCallback_);
        if (pipelineState && pipelineState->IsValid())
            batches.Insert(CreatePipelineBatch(key, pipelineState));
    }
}

BatchCompositor::BatchCompositor(RenderPipelineInterface* renderPipeline,
    const DrawableProcessor* drawableProcessor, BatchStateCacheCallback* callback, unsigned shadowPassIndex)
    : Object(renderPipeline->GetContext())
    , shadowPassIndex_(shadowPassIndex)
    , workQueue_(GetSubsystem<WorkQueue>())
    , renderer_(GetSubsystem<Renderer>())
    , drawableProcessor_(drawableProcessor)
    , defaultMaterial_(renderer_->GetDefaultMaterial())
    , lightVolumePass_(MakeShared<Pass>(""))
    , batchStateCacheCallback_(callback)
{
    lightVolumePass_->SetVertexShader("DeferredLight");
    lightVolumePass_->SetPixelShader("DeferredLight");

    renderPipeline->OnUpdateBegin.Subscribe(this, &BatchCompositor::OnUpdateBegin);
    renderPipeline->OnPipelineStatesInvalidated.Subscribe(this, &BatchCompositor::OnPipelineStatesInvalidated);
}

void BatchCompositor::SetPasses(ea::vector<SharedPtr<BatchCompositorPass>> passes)
{
    passes_ = passes;
}

void BatchCompositor::ComposeShadowBatches()
{
    // Collect shadow caster batches in worker threads
    const auto& lightProcessors = drawableProcessor_->GetLightProcessors();
    for (unsigned lightIndex = 0; lightIndex < lightProcessors.size(); ++lightIndex)
    {
        LightProcessor* lightProcessor = lightProcessors[lightIndex];
        const unsigned numSplits = lightProcessor->GetNumSplits();
        for (unsigned splitIndex = 0; splitIndex < numSplits; ++splitIndex)
        {
            workQueue_->AddWorkItem([=](unsigned threadIndex)
            {
                BeginShadowBatchesComposition(lightIndex, lightProcessor->GetMutableSplit(splitIndex));
            }, M_MAX_UNSIGNED);
        }
    }
    workQueue_->Complete(M_MAX_UNSIGNED);

    // Finalize shadow batches in main thread
    FinalizeShadowBatchesComposition();
}

void BatchCompositor::ComposeSceneBatches()
{
    for (BatchCompositorPass* pass : passes_)
        pass->ComposeBatches();
}

void BatchCompositor::ComposeLightVolumeBatches()
{
    BatchStateCreateContext ctx;
    ctx.pass_ = this;
    ctx.subpassIndex_ = LitVolumeSubpass;

    const auto& lightProcessors = drawableProcessor_->GetLightProcessors();
    for (unsigned lightIndex = 0; lightIndex < lightProcessors.size(); ++lightIndex)
    {
        LightProcessor* lightProcessor = lightProcessors[lightIndex];
        if (!lightProcessor->HasLitGeometries())
            continue;

        BatchStateCreateKey key;
        key.drawableHash_ = 0;
        key.sourceBatchIndex_ = M_MAX_UNSIGNED;
        key.pixelLightHash_ = lightProcessor->GetLightVolumeHash();
        key.geometryType_ = GEOM_STATIC_NOINSTANCING;
        key.geometry_ = renderer_->GetLightGeometry(lightProcessor->GetLight());
        key.material_ = defaultMaterial_;
        key.pass_ = lightVolumePass_;
        key.drawable_ = lightProcessor->GetLight();
        key.pixelLight_ = lightProcessor;
        key.pixelLightIndex_ = lightIndex;
        key.vertexLightsHash_ = 0;

        PipelineState* pipelineState = lightVolumeCache_.GetOrCreatePipelineState(key, ctx, batchStateCacheCallback_);
        if (pipelineState && pipelineState->IsValid())
            lightVolumeBatches_.push_back(CreatePipelineBatch(key, pipelineState));
    }

    SortBatches(sortedLightVolumeBatches_, lightVolumeBatches_);
}

void BatchCompositor::OnUpdateBegin(const CommonFrameInfo& frameInfo)
{
    delayedShadowBatches_.Clear();
    lightVolumeBatches_.clear();
    sortedLightVolumeBatches_.clear();
}

void BatchCompositor::OnPipelineStatesInvalidated()
{
    shadowCache_.Invalidate();
    lightVolumeCache_.Invalidate();
}

void BatchCompositor::BeginShadowBatchesComposition(unsigned lightIndex, ShadowSplitProcessor* splitProcessor)
{
    LightProcessor* lightProcessor = splitProcessor->GetLightProcessor();
    const unsigned lightHash = lightProcessor->GetShadowHash(splitProcessor->GetSplitIndex());

    const unsigned threadIndex = WorkQueue::GetThreadIndex();
    const auto& shadowCasters = splitProcessor->GetShadowCasters();
    auto& shadowBatches = splitProcessor->GetMutableShadowBatches();
    const unsigned lightMask = splitProcessor->GetLight()->GetLightMask();

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
            Technique* tech = material->FindTechnique(drawable, shadowMaterialQuality_);
            Pass* pass = tech->GetSupportedPass(shadowPassIndex_);
            if (!pass)
                continue;

            BatchStateCreateKey key;
            key.drawableHash_ = drawable->GetPipelineStateHash();
            key.pixelLightHash_ = lightHash;
            key.geometryType_ = sourceBatch.geometryType_;
            key.geometry_ = sourceBatch.geometry_;
            key.material_ = sourceBatch.material_ ? sourceBatch.material_.Get() : defaultMaterial_;
            key.pass_ = pass;

            key.drawable_ = drawable;
            key.sourceBatchIndex_ = j;
            key.pixelLight_ = lightProcessor;
            key.pixelLightIndex_ = lightIndex;
            key.vertexLightsHash_ = 0;

            PipelineState* pipelineState = shadowCache_.GetPipelineState(key);
            if (pipelineState)
            {
                if (pipelineState->IsValid())
                    shadowBatches.push_back(CreatePipelineBatch(key, pipelineState, CreateBatchTag::Unlit));
            }
            else
                delayedShadowBatches_.PushBack(threadIndex, { splitProcessor, key });
        }
    }
}

void BatchCompositor::FinalizeShadowBatchesComposition()
{
    BatchStateCreateContext ctx;
    ctx.pass_ = this;
    ctx.subpassIndex_ = ShadowSubpass;

    for (const auto& splitAndKey : delayedShadowBatches_)
    {
        const BatchStateCreateKey& key = splitAndKey.second;
        ShadowSplitProcessor& split = *splitAndKey.first;
        ctx.shadowSplitIndex_ = split.GetSplitIndex();
        PipelineState* pipelineState = shadowCache_.GetOrCreatePipelineState(key, ctx, batchStateCacheCallback_);
        if (pipelineState && pipelineState->IsValid())
            split.GetMutableShadowBatches().push_back(CreatePipelineBatch(key, pipelineState, CreateBatchTag::Unlit));
    }
}

}
