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

/*#include "../Core/Object.h"
#include "../RenderPipeline/LightAccumulator.h"
#include "../RenderPipeline/SceneBatch.h"
#include "../RenderPipeline/LightProcessor.h"*/
#include "../Graphics/GraphicsDefs.h"
#include "../RenderPipeline/BatchStateCache.h"
#include "../RenderPipeline/DrawableProcessor.h"
/*
#include "../Graphics/Light.h"
#include "../Graphics/SceneDrawableData.h"
#include "../Graphics/ShadowMapAllocator.h"
#include "../Graphics/PipelineStateTracker.h"
#include "../Graphics/Camera.h"
#include "../Scene/Node.h"*/

/*#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/sort.h>*/

namespace Urho3D
{

class Drawable;
class Geometry;
class Material;
class Pass;
class PipelineState;
struct SceneLightShadowSplit;

/// Self-sufficient batch that can be rendered by RenderPipeline.
// TODO(renderer): Sort by vertex lights?
struct PipelineBatch
{
    /// Index of per-pixel forward light in DrawableProcessor.
    unsigned lightIndex_{ M_MAX_UNSIGNED };
    /// Drawable index.
    unsigned drawableIndex_{};
    /// Index of source batch within Drawable.
    unsigned sourceBatchIndex_{};
    /// Geometry type used.
    GeometryType geometryType_{};
    /// Pointer to Drawable.
    Drawable* drawable_{};
    /// Pointer to Geometry.
    Geometry* geometry_{};
    /// Pointer to Material.
    Material* material_{};
    /// Pipeline state used for rendering.
    PipelineState* pipelineState_{};

    /// Return source batch.
    const SourceBatch& GetSourceBatch() const { return drawable_->GetBatches()[sourceBatchIndex_]; }
};

/// Batch composition utility of scene pass.
class BatchCompositorPass : public DrawableProcessorPass
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
    BatchCompositorPass(RenderPipeline* renderPipeline, const DrawableProcessor* drawableProcessor, bool needAmbient,
        unsigned unlitBasePassIndex, unsigned litBasePassIndex, unsigned lightPassIndex);

    /// Compose batches.
    void ComposeBatches();

protected:
    /// Called when update begins.
    void OnUpdateBegin(const FrameInfo& frameInfo) override;
    /// Called when pipeline states are invalidated.
    virtual void OnPipelineStatesInvalidated();

    /// Work queue.
    WorkQueue* workQueue_{};
    /// Default material.
    Material* defaultMaterial_{};
    /// Drawable processor.
    const DrawableProcessor* drawableProcessor_{};
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
    void InitializeKeyLight(BatchStateCreateKey& key, unsigned lightIndex) const;
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

/// Batch composition utility for shadow batches.
class BatchCompositor : public Object
{
    URHO3D_OBJECT(BatchCompositor, Object);

public:
    /// Construct.
    explicit BatchCompositor(RenderPipeline* renderPipeline, const DrawableProcessor* drawableProcessor,
        unsigned shadowPassIndex);
    /// Set passes.
    void SetPasses(ea::vector<SharedPtr<BatchCompositorPass>> passes);

    /// Compose batches.
    void ComposeBatches();

    /// Begin shadow batches composition. Safe to call from worker thread.
    void BeginShadowBatchesComposition(unsigned lightIndex, unsigned splitIndex);
    /// Finalize shadow batches composition. Should be called from main thread.
    void FinalizeShadowBatchesComposition();

protected:
    /// Called when update begins.
    virtual void OnUpdateBegin(const FrameInfo& frameInfo);
    /// Called when pipeline states are invalidated.
    virtual void OnPipelineStatesInvalidated();

private:
    /// Index of shadow technique pass.
    const unsigned shadowPassIndex_;

    /// Drawable processor.
    const DrawableProcessor* drawableProcessor_{};
    /// Default material.
    Material* defaultMaterial_{};
    /// Batch state creation callback.
    BatchStateCacheCallback* batchStateCacheCallback_{};

    /// Passes.
    ea::vector<SharedPtr<BatchCompositorPass>> passes_;

    /// Material quality.
    MaterialQuality materialQuality_{};

    /// Cache for shadow batches.
    BatchStateCache shadowCache_;
    /// Delayed shadow batches.
    WorkQueueVector<ea::pair<SceneLightShadowSplit*, BatchStateCreateKey>> delayedShadowBatches_;
};

}
