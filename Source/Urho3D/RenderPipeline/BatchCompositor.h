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

#include <EASTL/sort.h>

namespace Urho3D
{

class Drawable;
class Geometry;
class LightProcessor;
class Material;
class Pass;
class PipelineState;
class WorkQueue;
struct PipelineBatchByState;
struct ShadowSplitProcessor;

/// Self-sufficient batch that can be rendered by RenderPipeline.
struct PipelineBatch
{
    /// Pointer to Drawable.
    Drawable* drawable_{};
    /// Pointer to Geometry.
    Geometry* geometry_{};
    /// Pointer to Material.
    Material* material_{};
    /// Pipeline state used for rendering.
    PipelineState* pipelineState_{};
    /// Drawable index.
    unsigned drawableIndex_{};
    /// Index of per-pixel forward light in DrawableProcessor.
    unsigned lightIndex_{ M_MAX_UNSIGNED };
    /// Order-independent hash of vertex lights.
    unsigned vertexLightsHash_{};
    /// Index of source batch within Drawable.
    unsigned sourceBatchIndex_{};
    /// Geometry type used.
    GeometryType geometryType_{};

    /// Return source batch.
    const SourceBatch& GetSourceBatch() const { return drawable_->GetBatches()[sourceBatchIndex_]; }
};

/// Batch compositor for single scene pass.
class URHO3D_API BatchCompositorPass : public DrawableProcessorPass
{
    URHO3D_OBJECT(BatchCompositorPass, DrawableProcessorPass);

public:
    /// Subpass indices.
    enum : unsigned
    {
        UnlitBaseSubpass = 0,
        LitBaseSubpass,
        LightSubpass,
    };

    /// Construct.
    BatchCompositorPass(RenderPipelineInterface* renderPipeline, DrawableProcessor* drawableProcessor,
        bool needAmbient, unsigned unlitBasePassIndex, unsigned litBasePassIndex, unsigned lightPassIndex);

    /// Compose batches.
    void ComposeBatches();

protected:
    /// Called when update begins.
    void OnUpdateBegin(const FrameInfo& frameInfo) override;
    /// Called when pipeline states are invalidated.
    virtual void OnPipelineStatesInvalidated();
    /// Called when batches are ready.
    virtual void OnBatchesReady() {}

    /// Work queue.
    WorkQueue* workQueue_{};
    /// Default material.
    Material* defaultMaterial_{};
    /// Drawable processor.
    DrawableProcessor* drawableProcessor_{};
    /// Batch state creation callback.
    BatchStateCacheCallback* batchStateCacheCallback_{};

    /// Unlit and lit base batches.
    WorkQueueVector<PipelineBatch> baseBatches_;
    /// Light batches.
    WorkQueueVector<PipelineBatch> lightBatches_;

private:
    /// Initialize common members of BatchStateCreateKey.
    bool InitializeKey(BatchStateCreateKey& key, const GeometryBatch& geometryBatch) const;
    /// Initialize light-related members of BatchStateCreateKey.
    void InitializeKeyLight(BatchStateCreateKey& key, unsigned lightIndex, unsigned vertexLightsHash) const;
    /// Process geometry batch (threaded).
    void ProcessGeometryBatch(const GeometryBatch& geometryBatch);
    /// Resolve delayed batches.
    void ResolveDelayedBatches(unsigned index, const WorkQueueVector<BatchStateCreateKey>& delayedBatches,
        BatchStateCache& cache, WorkQueueVector<PipelineBatch>& batches);

    /// Cache for unlit base batches.
    BatchStateCache unlitBaseCache_;
    /// Cache for lit base batches.
    BatchStateCache litBaseCache_;
    /// Cache for light batches.
    BatchStateCache lightCache_;

    /// Delayed unlit base batches.
    WorkQueueVector<BatchStateCreateKey> delayedUnlitBaseBatches_;
    /// Delayed lit base batches.
    WorkQueueVector<BatchStateCreateKey> delayedLitBaseBatches_;
    /// Delayed light batches.
    WorkQueueVector<BatchStateCreateKey> delayedLightBatches_;
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

    /// Construct.
    BatchCompositor(RenderPipelineInterface* renderPipeline, const DrawableProcessor* drawableProcessor,
        unsigned shadowPassIndex);
    /// Set passes.
    void SetPasses(ea::vector<SharedPtr<BatchCompositorPass>> passes);

    /// Compose shadow batches.
    void ComposeShadowBatches();
    /// Compose scene batches.
    void ComposeSceneBatches();
    /// Compose light volume batches.
    void ComposeLightVolumeBatches();

    /// Return sorted light volume batches.
    const auto& GetLightVolumeBatches() const { return sortedLightVolumeBatches_; }

    /// Sort batches (from vector).
    template <class T, class ... U>
    static void SortBatches(ea::vector<T>& sortedBatches, const U& ... pipelineBatches)
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

        // Sort
        ea::sort(sortedBatches.begin(), sortedBatches.end());
    }

    /// Sort batches (from WorkQueueVector).
    template <class T>
    static void SortBatches(const WorkQueueVector<PipelineBatch>& sceneBatches, ea::vector<T>& sortedBatches)
    {
        const unsigned numBatches = sceneBatches.Size();
        sortedBatches.resize(numBatches);
        unsigned elementIndex = 0;
        for (const PipelineBatch& lightBatch : sceneBatches)
        {
            sortedBatches[elementIndex] = T{ &lightBatch };
            ++elementIndex;
        }
        ea::sort(sortedBatches.begin(), sortedBatches.end());
    }

protected:
    /// Called when update begins.
    virtual void OnUpdateBegin(const FrameInfo& frameInfo);
    /// Called when pipeline states are invalidated.
    virtual void OnPipelineStatesInvalidated();

    /// Begin shadow batches composition. Safe to call from worker thread.
    void BeginShadowBatchesComposition(unsigned lightIndex, ShadowSplitProcessor* splitProcessor);
    /// Finalize shadow batches composition. Should be called from main thread.
    void FinalizeShadowBatchesComposition();

private:
    /// Index of shadow technique pass.
    const unsigned shadowPassIndex_;

    /// Work queue.
    WorkQueue* workQueue_{};
    /// Renderer.
    Renderer* renderer_{};

    /// Drawable processor.
    const DrawableProcessor* drawableProcessor_{};
    /// Default material.
    Material* defaultMaterial_{};
    /// Null pass (for light volumes).
    SharedPtr<Pass> nullPass_{};
    /// Batch state creation callback.
    BatchStateCacheCallback* batchStateCacheCallback_{};

    /// Passes.
    ea::vector<SharedPtr<BatchCompositorPass>> passes_;

    /// Material quality.
    MaterialQuality materialQuality_{};

    /// Cache for shadow batches.
    BatchStateCache shadowCache_;
    /// Delayed shadow batches.
    WorkQueueVector<ea::pair<ShadowSplitProcessor*, BatchStateCreateKey>> delayedShadowBatches_;

    /// Cache for light volume batches.
    BatchStateCache lightVolumeCache_;
    /// Light volume batches.
    ea::vector<PipelineBatch> lightVolumeBatches_;
    /// Sorted light volume batches.
    ea::vector<PipelineBatchByState> sortedLightVolumeBatches_;
};

}
