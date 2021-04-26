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

#pragma once

#include "../Graphics/GraphicsDefs.h"
#include "../RenderPipeline/BatchStateCache.h"
#include "../RenderPipeline/DrawableProcessor.h"
#include "../RenderPipeline/InstancingBuffer.h"

#include <EASTL/sort.h>

namespace Urho3D
{

class Drawable;
class Geometry;
class LightProcessor;
class Material;
class Pass;
class PipelineState;
class ShadowSplitProcessor;
class WorkQueue;
struct PipelineBatchByState;

/// Self-sufficient batch that can be sorted and rendered by RenderPipeline.
struct PipelineBatch
{
    Drawable* drawable_{};
    Geometry* geometry_{};
    Material* material_{};
    PipelineState* pipelineState_{};
    unsigned drawableIndex_{};
    unsigned pixelLightIndex_{ M_MAX_UNSIGNED };
    unsigned vertexLightsHash_{};
    unsigned sourceBatchIndex_{};
    unsigned lightmapIndex_{};
    float distance_{};
    GeometryType geometryType_{};

    const SourceBatch& GetSourceBatch() const
    {
        static const SourceBatch defaultSourceBatch;
        return sourceBatchIndex_ != M_MAX_UNSIGNED ? drawable_->GetBatches()[sourceBatchIndex_] : defaultSourceBatch;
    }

    PipelineBatch() = default;
    PipelineBatch(Drawable* drawable, unsigned sourceBatchIndex)
    {
        drawable_ = drawable;
        drawableIndex_ = drawable_->GetDrawableIndex();
        sourceBatchIndex_ = sourceBatchIndex;

        const SourceBatch& sourceBatch = GetSourceBatch();
        geometry_ = sourceBatch.geometry_;
        material_ = sourceBatch.material_;
        lightmapIndex_ = sourceBatch.lightmapIndex_;
        distance_ = sourceBatch.distance_;
        geometryType_ = sourceBatch.geometryType_;
    }
};

/// Information needed to fully create PipelineBatch.
struct PipelineBatchDesc : public PipelineBatch
{
    Pass* pass_{};
    unsigned drawableHash_{};
    /// Light that contributes to pipeline state.
    /// For scene batches: per-pixel forward light applied to object.
    /// For shadow batches: owner shadow split.
    /// @{
    LightProcessor* pixelLightForPipelineState_{};
    unsigned pixelLightForPipelineStateIndex_{};
    unsigned pixelLightForPipelineStateHash_{};
    /// @}

    PipelineBatchDesc() = default;
    PipelineBatchDesc(Drawable* drawable, unsigned sourceBatchIndex, Pass* pass)
        : PipelineBatch(drawable, sourceBatchIndex)
        , pass_(pass)
        , drawableHash_(drawable->GetPipelineStateHash())
    {
    }

    void InitializeShadowBatch(LightProcessor* light, unsigned lightIndex, unsigned lightHash)
    {
        pixelLightForPipelineState_ = light;
        pixelLightForPipelineStateIndex_ = lightIndex;
        pixelLightForPipelineStateHash_ = lightHash;
    }

    void InitializeLitBatch(LightProcessor* light, unsigned lightIndex, unsigned lightHash)
    {
        pixelLightIndex_ = lightIndex;
        pixelLightForPipelineState_ = light;
        pixelLightForPipelineStateIndex_ = lightIndex;
        pixelLightForPipelineStateHash_ = lightHash;
    }

    BatchStateCreateKey GetKey() const
    {
        BatchStateCreateKey key;
        key.drawableHash_ = drawableHash_;
        key.pixelLightHash_ = pixelLightForPipelineStateHash_;
        key.geometryType_ = geometryType_;
        key.geometry_ = geometry_;
        key.material_ = material_;
        key.pass_ = pass_;
        key.drawable_ = drawable_;
        key.pixelLight_ = pixelLightForPipelineState_;
        key.pixelLightIndex_ = pixelLightForPipelineStateIndex_;
        return key;
    }
};

/// Batch compositor for single scene pass.
class URHO3D_API BatchCompositorPass : public DrawableProcessorPass
{
    URHO3D_OBJECT(BatchCompositorPass, DrawableProcessorPass);

public:
    BatchCompositorPass(RenderPipelineInterface* renderPipeline,
        DrawableProcessor* drawableProcessor, BatchStateCacheCallback* callback, DrawableProcessorPassFlags flags,
        unsigned deferredPassIndex, unsigned unlitBasePassIndex, unsigned litBasePassIndex, unsigned lightPassIndex);

    void ComposeBatches();

    bool HasBatches() const
    {
        return deferredBatches_.Size() > 0
            || baseBatches_.Size() > 0
            || lightBatches_.Size() > 0;
        }

protected:
    /// Callbacks from RenderPipeline
    /// @{
    void OnUpdateBegin(const CommonFrameInfo& frameInfo) override;
    virtual void OnPipelineStatesInvalidated();
    /// @}

    /// Called when batches are ready.
    virtual void OnBatchesReady() {}

    /// External dependencies
    /// @{
    WorkQueue* workQueue_{};
    Material* defaultMaterial_{};
    DrawableProcessor* drawableProcessor_{};
    BatchStateCacheCallback* batchStateCacheCallback_{};
    /// @}

    WorkQueueVector<PipelineBatch> deferredBatches_;
    WorkQueueVector<PipelineBatch> baseBatches_;
    WorkQueueVector<PipelineBatch> lightBatches_;
    WorkQueueVector<PipelineBatch> negativeLightBatches_;

private:
    bool PreparePipelineBatch(PipelineBatchDesc& key, const GeometryBatch& geometryBatch) const;

    void ProcessGeometryBatch(const GeometryBatch& geometryBatch);
    void ResolveDelayedBatches(BatchCompositorSubpass subpass, const WorkQueueVector<PipelineBatchDesc>& delayedBatches,
        BatchStateCache& cache, WorkQueueVector<PipelineBatch>& batches);

    /// Pipeline state caches
    /// @{
    BatchStateCache deferredCache_;
    BatchStateCache unlitBaseCache_;
    BatchStateCache litBaseCache_;
    BatchStateCache lightCache_;
    /// @}

    /// Batches whose processing is delayed due to missing pipeline state
    /// @{
    WorkQueueVector<PipelineBatchDesc> delayedDeferredBatches_;
    WorkQueueVector<PipelineBatchDesc> delayedUnlitBaseBatches_;
    WorkQueueVector<PipelineBatchDesc> delayedLitBaseBatches_;
    WorkQueueVector<PipelineBatchDesc> delayedLightBatches_;
    WorkQueueVector<PipelineBatchDesc> delayedNegativeLightBatches_;
    /// @}
};

/// Batch composition manager.
class URHO3D_API BatchCompositor : public Object
{
    URHO3D_OBJECT(BatchCompositor, Object);

public:
    /// Subpass indices.
    enum : unsigned
    {
        ShadowSubpass = 0,
        LitVolumeSubpass
    };

    BatchCompositor(RenderPipelineInterface* renderPipeline,
        const DrawableProcessor* drawableProcessor, BatchStateCacheCallback* callback, unsigned shadowPassIndex);
    ~BatchCompositor() override;
    void SetPasses(ea::vector<SharedPtr<BatchCompositorPass>> passes);
    void SetShadowMaterialQuality(MaterialQuality materialQuality) { shadowMaterialQuality_ = materialQuality; }

    /// Compose batches
    /// @{
    void ComposeShadowBatches();
    void ComposeSceneBatches();
    void ComposeLightVolumeBatches();
    /// @}

    /// Return sorted light volume batches.
    const auto& GetLightVolumeBatches() const { return sortedLightVolumeBatches_; }

    /// Prepare vector of sorted batches w/o actual sorting.
    template <class T, class ... U>
    static void FillSortKeys(ea::vector<T>& sortedBatches, const U& ... pipelineBatches)
    {
        using namespace std;
        const auto pipelineBatchesArray = { &pipelineBatches... };

        // Evaluate total size
        unsigned totalSize = 0;
        for (const auto* batches : pipelineBatchesArray)
            totalSize += size(*batches);

        // Build sorted batches
        sortedBatches.resize(totalSize);
        unsigned i = 0;
        for (const auto* batches : pipelineBatchesArray)
        {
            for (const auto& pipelineBatch : *batches)
                sortedBatches[i++] = T{ &pipelineBatch };
        }
    }

protected:
    /// Callbacks from RenderPipeline
    /// @{
    virtual void OnUpdateBegin(const CommonFrameInfo& frameInfo);
    virtual void OnPipelineStatesInvalidated();
    /// @}

    /// Safe to call from worker thread.
    void BeginShadowBatchesComposition(unsigned lightIndex, ShadowSplitProcessor* splitProcessor);
    /// Should be called from main thread.
    void FinalizeShadowBatchesComposition();

private:
    const unsigned shadowPassIndex_;

    /// External dependencies
    /// @{
    WorkQueue* workQueue_{};
    Renderer* renderer_{};
    const DrawableProcessor* drawableProcessor_{};
    Material* defaultMaterial_{};
    BatchStateCacheCallback* batchStateCacheCallback_{};
    /// @}

    /// Cached between frames
    /// @{
    ea::vector<SharedPtr<BatchCompositorPass>> passes_;
    MaterialQuality shadowMaterialQuality_{};
    SharedPtr<Material> lightVolumeMaterial_;
    SharedPtr<Material> negativeLightVolumeMaterial_;
    SharedPtr<Pass> lightVolumePass_;

    BatchStateCache shadowCache_;
    BatchStateCache lightVolumeCache_;
    /// @}

    WorkQueueVector<ea::pair<ShadowSplitProcessor*, PipelineBatchDesc>> delayedShadowBatches_;
    ea::vector<PipelineBatch> lightVolumeBatches_;
    ea::vector<PipelineBatchByState> sortedLightVolumeBatches_;
};

}
