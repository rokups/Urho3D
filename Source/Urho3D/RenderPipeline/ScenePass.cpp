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
#include "../Graphics/Renderer.h"
#include "../Graphics/Technique.h"
#include "../RenderPipeline/BatchRenderer.h"
#include "../RenderPipeline/ScenePass.h"

#include <EASTL/sort.h>

#include "../DebugNew.h"

namespace Urho3D
{

ScenePass::ScenePass(RenderPipelineInterface* renderPipeline, DrawableProcessor* drawableProcessor,
    BatchStateCacheCallback* callback, DrawableProcessorPassFlags flags, const ea::string& deferredPass,
    const ea::string& unlitBasePass, const ea::string& litBasePass, const ea::string& lightPass)
    : BatchCompositorPass(renderPipeline, drawableProcessor, callback, flags, Technique::GetPassIndex(deferredPass),
        Technique::GetPassIndex(unlitBasePass), Technique::GetPassIndex(litBasePass), Technique::GetPassIndex(lightPass))
{
}

ScenePass::ScenePass(RenderPipelineInterface* renderPipeline, DrawableProcessor* drawableProcessor,
    BatchStateCacheCallback* callback, DrawableProcessorPassFlags flags, const ea::string& pass)
    : BatchCompositorPass(renderPipeline, drawableProcessor, callback, flags, M_MAX_UNSIGNED,
        Technique::GetPassIndex(pass), M_MAX_UNSIGNED, M_MAX_UNSIGNED)
{
}

void UnorderedScenePass::OnBatchesReady()
{
    BatchCompositor::FillSortKeys(sortedDeferredBatches_, deferredBatches_);
    BatchCompositor::FillSortKeys(sortedBaseBatches_, baseBatches_);
    BatchCompositor::FillSortKeys(sortedLightBatches_, lightBatches_, negativeLightBatches_);

    ea::sort(sortedDeferredBatches_.begin(), sortedDeferredBatches_.end());
    ea::sort(sortedBaseBatches_.begin(), sortedBaseBatches_.end());

    const unsigned numNegativeLightBatches = negativeLightBatches_.Size();
    ea::sort(sortedLightBatches_.begin(), sortedLightBatches_.end() - numNegativeLightBatches);
    ea::sort(sortedLightBatches_.end() - numNegativeLightBatches, sortedLightBatches_.end());

    deferredBatchGroup_ = { sortedDeferredBatches_ };
    baseBatchGroup_ = { sortedBaseBatches_ };
    lightBatchGroup_ = { sortedLightBatches_ };

    if (!GetFlags().Test(DrawableProcessorPassFlag::DisableInstancing))
    {
        deferredBatchGroup_.flags_ |= BatchRenderFlag::EnableInstancingForStaticGeometry;
        baseBatchGroup_.flags_ |= BatchRenderFlag::EnableInstancingForStaticGeometry;
        lightBatchGroup_.flags_ |= BatchRenderFlag::EnableInstancingForStaticGeometry;
    }

    if (HasLightPass())
    {
        baseBatchGroup_.flags_ |= BatchRenderFlag::EnablePixelLights;
        lightBatchGroup_.flags_ |= BatchRenderFlag::EnablePixelLights;
    }

    if (GetFlags().Test(DrawableProcessorPassFlag::HasAmbientLighting))
    {
        deferredBatchGroup_.flags_ |= BatchRenderFlag::EnableAmbientLighting;
        baseBatchGroup_.flags_ |= BatchRenderFlag::EnableAmbientAndVertexLighting;
    }

    if (GetFlags().Test(DrawableProcessorPassFlag::DepthOnlyPass))
    {
        deferredBatchGroup_.flags_ |= BatchRenderFlag::DisableColorOutput;
        baseBatchGroup_.flags_ |= BatchRenderFlag::DisableColorOutput;
    }
}

void UnorderedScenePass::PrepareInstacingBuffer(BatchRenderer* batchRenderer)
{
    batchRenderer->PrepareInstancingBuffer(deferredBatchGroup_);
    batchRenderer->PrepareInstancingBuffer(baseBatchGroup_);
    batchRenderer->PrepareInstancingBuffer(lightBatchGroup_);
}

void BackToFrontScenePass::OnBatchesReady()
{
    BatchCompositor::FillSortKeys(sortedBatches_, baseBatches_, lightBatches_, negativeLightBatches_);

    // When rendering back-to-front, it's still important to render batches for each object in order:
    // - Base batch;
    // - Additive light batches;
    // - Substractive light batches.
    // Multiply distance by some factor close to 1 if distance is greater than 0, ignore otherwise.

    const unsigned substractiveLightBatchesBegin = sortedBatches_.size() - negativeLightBatches_.Size();
    const unsigned substractiveLightBatchesEnd = sortedBatches_.size();
    const unsigned additiveLightBatchesBegin = substractiveLightBatchesBegin - lightBatches_.Size();
    const unsigned additiveLightBatchesEnd = substractiveLightBatchesBegin;

    static const float additiveDistanceFactor = 1 - M_EPSILON;
    static const float substractiveDistanceFactor = 1 - 2 * M_EPSILON;

    for (unsigned i = additiveLightBatchesBegin; i < additiveLightBatchesEnd; ++i)
        sortedBatches_[i].distance_ *= additiveDistanceFactor;

    for (unsigned i = substractiveLightBatchesBegin; i < substractiveLightBatchesEnd; ++i)
        sortedBatches_[i].distance_ *= substractiveDistanceFactor;

    ea::sort(sortedBatches_.begin(), sortedBatches_.end());

    if (GetFlags().Test(DrawableProcessorPassFlag::RefractionPass))
    {
        hasRefractionBatches_ = false;
        for (const PipelineBatchBackToFront& sortedBatch : sortedBatches_)
        {
            // Assume refraction if blending is disabled
            if (sortedBatch.pipelineBatch_->pipelineState_->GetDesc().blendMode_ == BLEND_REPLACE)
            {
                hasRefractionBatches_ = true;
                break;
            }
        }
    }

    batchGroup_ = { sortedBatches_ };
    if (GetFlags().Test(DrawableProcessorPassFlag::HasAmbientLighting))
        batchGroup_.flags_ |= BatchRenderFlag::EnableAmbientAndVertexLighting;
    if (HasLightPass())
        batchGroup_.flags_ |= BatchRenderFlag::EnablePixelLights;
    if (!GetFlags().Test(DrawableProcessorPassFlag::DisableInstancing))
        batchGroup_.flags_ |= BatchRenderFlag::EnableInstancingForStaticGeometry;
}

void BackToFrontScenePass::PrepareInstacingBuffer(BatchRenderer* batchRenderer)
{
    batchRenderer->PrepareInstancingBuffer(batchGroup_);
}

}
