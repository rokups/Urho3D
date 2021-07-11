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

/// Add batch or delayed batch.
void AddPipelineBatch(const PipelineBatchDesc& desc, BatchStateCache& cache,
    WorkQueueVector<PipelineBatch>& batches, WorkQueueVector<PipelineBatchDesc>& delayedBatches)
{
    PipelineState* pipelineState = cache.GetPipelineState(desc.GetKey());
    if (pipelineState)
    {
        if (pipelineState->IsValid())
        {
            PipelineBatch& pipelineBatch = batches.Emplace(desc);
            pipelineBatch.pipelineState_ = pipelineState;
        }
    }
    else
        delayedBatches.Insert(desc);
}

}

BatchCompositorPass::BatchCompositorPass(RenderPipelineInterface* renderPipeline,
    DrawableProcessor* drawableProcessor, BatchStateCacheCallback* callback, DrawableProcessorPassFlags flags,
    unsigned deferredPassIndex, unsigned unlitBasePassIndex, unsigned litBasePassIndex, unsigned lightPassIndex)
    : DrawableProcessorPass(renderPipeline, flags, deferredPassIndex, unlitBasePassIndex, litBasePassIndex, lightPassIndex)
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
    ResolveDelayedBatches(BatchCompositorSubpass::Deferred, delayedDeferredBatches_, deferredCache_, deferredBatches_);
    ResolveDelayedBatches(BatchCompositorSubpass::Base, delayedUnlitBaseBatches_, unlitBaseCache_, baseBatches_);
    ResolveDelayedBatches(BatchCompositorSubpass::Base, delayedLitBaseBatches_, litBaseCache_, baseBatches_);
    ResolveDelayedBatches(BatchCompositorSubpass::Light, delayedLightBatches_, lightCache_, lightBatches_);
    ResolveDelayedBatches(BatchCompositorSubpass::Light, delayedNegativeLightBatches_, lightCache_, negativeLightBatches_);

    OnBatchesReady();
}

void BatchCompositorPass::OnUpdateBegin(const CommonFrameInfo& frameInfo)
{
    BaseClassName::OnUpdateBegin(frameInfo);

    deferredBatches_.Clear();
    baseBatches_.Clear();
    lightBatches_.Clear();
    negativeLightBatches_.Clear();

    delayedDeferredBatches_.Clear();
    delayedUnlitBaseBatches_.Clear();
    delayedLitBaseBatches_.Clear();
    delayedLightBatches_.Clear();
    delayedNegativeLightBatches_.Clear();
}

void BatchCompositorPass::OnPipelineStatesInvalidated()
{
    deferredCache_.Invalidate();
    unlitBaseCache_.Invalidate();
    litBaseCache_.Invalidate();
    lightCache_.Invalidate();
}

void BatchCompositorPass::ProcessGeometryBatch(const GeometryBatch& geometryBatch)
{
    // Skip invalid batches. It may happen if UpdateGeometry removed some source batches.
    PipelineBatchDesc desc(geometryBatch.drawable_, geometryBatch.sourceBatchIndex_, geometryBatch.deferredPass_);
    if (!desc.geometry_)
        return;

    if (!desc.material_)
        desc.material_ = defaultMaterial_;

    // Always add deferred batch if possible.
    if (desc.pass_)
    {
        AddPipelineBatch(desc, deferredCache_, deferredBatches_, delayedDeferredBatches_);
        return;
    }

    // Process forward lighting if applicable
    unsigned litBaseLightIndex = M_MAX_UNSIGNED;
    if (geometryBatch.lightPass_)
    {
        const unsigned drawableIndex = desc.drawable_->GetDrawableIndex();
        const LightAccumulator& lightAccumulator = drawableProcessor_->GetGeometryLighting(drawableIndex);
        const auto pixelLights = lightAccumulator.GetPixelLights();

        // Add light batches
        desc.pass_ = geometryBatch.lightPass_;
        for (unsigned i = 0; i < pixelLights.size(); ++i)
        {
            const unsigned lightIndex = pixelLights[i].second;
            LightProcessor* lightProcessor = drawableProcessor_->GetLightProcessor(lightIndex);
            const Light* light = lightProcessor->GetLight();

            // Combine first directional additive light with base pass, if possible
            if (geometryBatch.litBasePass_ && litBaseLightIndex == M_MAX_UNSIGNED
                && light->GetLightType() == LIGHT_DIRECTIONAL && !light->IsNegative())
            {
                litBaseLightIndex = lightIndex;
                continue;
            }

            desc.InitializeLitBatch(lightProcessor, lightIndex, lightProcessor->GetForwardLitHash());

            if (lightProcessor->GetLight()->IsNegative())
                AddPipelineBatch(desc, lightCache_, negativeLightBatches_, delayedNegativeLightBatches_);
            else
                AddPipelineBatch(desc, lightCache_, lightBatches_, delayedLightBatches_);
        }

        // Initialize vertex lights after all light batches
        desc.vertexLightsHash_ = lightAccumulator.GetVertexLightsHash();
    }

    // Add base pass if needed
    if (litBaseLightIndex != M_MAX_UNSIGNED)
    {
        LightProcessor* light = drawableProcessor_->GetLightProcessor(litBaseLightIndex);
        desc.InitializeLitBatch(light, litBaseLightIndex, light->GetForwardLitHash());
        desc.pass_ = geometryBatch.litBasePass_;
        AddPipelineBatch(desc, litBaseCache_, baseBatches_, delayedLitBaseBatches_);
    }
    else
    {
        desc.InitializeLitBatch(nullptr, M_MAX_UNSIGNED, 0);
        desc.pass_ = geometryBatch.unlitBasePass_;
        AddPipelineBatch(desc, unlitBaseCache_, baseBatches_, delayedUnlitBaseBatches_);
    }
}

void BatchCompositorPass::ResolveDelayedBatches(BatchCompositorSubpass subpass,
    const WorkQueueVector<PipelineBatchDesc>& delayedBatches,
    BatchStateCache& cache, WorkQueueVector<PipelineBatch>& batches)
{
    BatchStateCreateContext ctx;
    ctx.pass_ = this;
    ctx.subpassIndex_ = static_cast<unsigned>(subpass);

    for (const PipelineBatchDesc& desc : delayedBatches)
    {
        PipelineState* pipelineState = cache.GetOrCreatePipelineState(desc.GetKey(), ctx, batchStateCacheCallback_);
        if (pipelineState && pipelineState->IsValid())
        {
            PipelineBatch& pipelineBatch = batches.Emplace(desc);
            pipelineBatch.pipelineState_ = pipelineState;
        }
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
    , lightVolumeMaterial_(defaultMaterial_->Clone("[Internal]/LightVolume"))
    , negativeLightVolumeMaterial_(defaultMaterial_->Clone("[Internal]/NegativeLightVolume"))
    , lightVolumePass_(MakeShared<Pass>("lightvolume"))
    , batchStateCacheCallback_(callback)
{
    negativeLightVolumeMaterial_->SetRenderOrder(DEFAULT_RENDER_ORDER + 1);
    lightVolumePass_->SetVertexShader("DeferredLight");
    lightVolumePass_->SetPixelShader("DeferredLight");

    renderPipeline->OnUpdateBegin.Subscribe(this, &BatchCompositor::OnUpdateBegin);
    renderPipeline->OnPipelineStatesInvalidated.Subscribe(this, &BatchCompositor::OnPipelineStatesInvalidated);
}

BatchCompositor::~BatchCompositor()
{
}

void BatchCompositor::SetPasses(ea::vector<SharedPtr<BatchCompositorPass>> passes)
{
    passes_ = passes;
}

void BatchCompositor::ComposeShadowBatches()
{
    URHO3D_PROFILE("PrepareShadowBatches");

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

    // Finalize shadow batches
    FinalizeShadowBatchesComposition();
}

void BatchCompositor::ComposeSceneBatches()
{
    URHO3D_PROFILE("PrepareSceneBatches");

    for (BatchCompositorPass* pass : passes_)
        pass->ComposeBatches();
}

void BatchCompositor::ComposeLightVolumeBatches()
{
    URHO3D_PROFILE("PrepareLightVolumeBatches");

    BatchStateCreateContext ctx;
    ctx.pass_ = this;
    ctx.subpassIndex_ = LitVolumeSubpass;

    const auto& lightProcessors = drawableProcessor_->GetLightProcessors();
    for (unsigned lightIndex = 0; lightIndex < lightProcessors.size(); ++lightIndex)
    {
        LightProcessor* lightProcessor = lightProcessors[lightIndex];
        if (!lightProcessor->HasLitGeometries())
            continue;

        Light* light = lightProcessor->GetLight();

        PipelineBatchDesc desc;
        desc.InitializeLitBatch(lightProcessor, lightIndex, lightProcessor->GetLightVolumeHash());
        desc.drawableHash_ = 0;
        desc.sourceBatchIndex_ = M_MAX_UNSIGNED;
        desc.geometryType_ = GEOM_STATIC_NOINSTANCING;
        desc.geometry_ = renderer_->GetLightGeometry(light);
        desc.material_ = light->IsNegative() ? negativeLightVolumeMaterial_ : lightVolumeMaterial_;
        desc.pass_ = lightVolumePass_;
        desc.drawable_ = light;
        desc.vertexLightsHash_ = 0;

        PipelineState* pipelineState = lightVolumeCache_.GetOrCreatePipelineState(desc.GetKey(), ctx, batchStateCacheCallback_);
        if (pipelineState && pipelineState->IsValid())
        {
            PipelineBatch& pipelineBatch = lightVolumeBatches_.emplace_back(desc);
            pipelineBatch.pipelineState_ = pipelineState;
        }
    }

    FillSortKeys(sortedLightVolumeBatches_, lightVolumeBatches_);
    ea::sort(sortedLightVolumeBatches_.begin(), sortedLightVolumeBatches_.end());
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
    auto& shadowBatches = splitProcessor->GetMutableUnsortedShadowBatches();
    const unsigned lightMask = splitProcessor->GetLight()->GetLightMask();

    for (Drawable* drawable : shadowCasters)
    {
        // Check shadow mask now when zone is ready
        if ((drawable->GetShadowMaskInZone() & lightMask) == 0)
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

            PipelineBatchDesc desc(drawable, j, pass);
            desc.material_ = material;
            desc.InitializeShadowBatch(lightProcessor, lightIndex, lightHash);

            PipelineState* pipelineState = shadowCache_.GetPipelineState(desc.GetKey());
            if (pipelineState)
            {
                if (pipelineState->IsValid())
                {
                    PipelineBatch& pipelineBatch = shadowBatches.emplace_back(desc);
                    pipelineBatch.pipelineState_ = pipelineState;
                }
            }
            else
                delayedShadowBatches_.PushBack(threadIndex, { splitProcessor, desc });
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
        const PipelineBatchDesc& desc = splitAndKey.second;
        ShadowSplitProcessor& split = *splitAndKey.first;
        ctx.shadowSplitIndex_ = split.GetSplitIndex();
        PipelineState* pipelineState = shadowCache_.GetOrCreatePipelineState(desc.GetKey(), ctx, batchStateCacheCallback_);
        if (pipelineState && pipelineState->IsValid())
        {
            PipelineBatch& pipelineBatch = split.GetMutableUnsortedShadowBatches().emplace_back(desc);
            pipelineBatch.pipelineState_ = pipelineState;
        }
    }

    // Finalize shadow batches
    const auto& lightProcessors = drawableProcessor_->GetLightProcessors();
    for (unsigned lightIndex = 0; lightIndex < lightProcessors.size(); ++lightIndex)
    {
        LightProcessor* lightProcessor = lightProcessors[lightIndex];
        const unsigned numSplits = lightProcessor->GetNumSplits();
        for (unsigned splitIndex = 0; splitIndex < numSplits; ++splitIndex)
        {
            workQueue_->AddWorkItem([=](unsigned threadIndex)
            {
                lightProcessor->GetMutableSplit(splitIndex)->FinalizeShadowBatches();
            }, M_MAX_UNSIGNED);
        }
    }
    workQueue_->Complete(M_MAX_UNSIGNED);
}

}
