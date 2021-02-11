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

#include "../Core/Object.h"
#include "../RenderPipeline/LightAccumulator.h"
#include "../RenderPipeline/SceneBatch.h"
#include "../RenderPipeline/LightProcessor.h"
#include "../RenderPipeline/BatchStateCache.h"
#include "../RenderPipeline/BatchCompositor.h"
#include "../RenderPipeline/DrawableProcessor.h"
/*
#include "../Graphics/Light.h"
#include "../Graphics/SceneDrawableData.h"
#include "../Graphics/ShadowMapAllocator.h"
#include "../Graphics/PipelineStateTracker.h"
#include "../Graphics/Camera.h"
#include "../Scene/Node.h"*/

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/sort.h>

namespace Urho3D
{

class Renderer;
class WorkQueue;

/// Scene pass interface.
class URHO3D_API ScenePass : public BatchCompositorPass
{
    URHO3D_OBJECT(ScenePass, BatchCompositorPass);

public:
    /// Max number of vertex lights for forward rendering.
    static const unsigned MaxVertexLights = 4;
    /// Max number of pixel lights for forward rendering. Soft limit, violation leads to performance penalty.
    static const unsigned MaxPixelLights = 4;
    /// Batch processing threshold.
    static const unsigned BatchThreshold = 10;
    /// Drawable lighting index.
    using DrawableLightingData = ea::vector<DrawableLightAccumulator<MaxPixelLights, MaxVertexLights>>;

    /// Construct.
    ScenePass(RenderPipeline* renderPipeline, const DrawableProcessor* drawableProcessor,
        const ea::string& unlitBaseTag, const ea::string& litBaseTag, const ea::string& lightTag,
        unsigned unlitBasePassIndex, unsigned litBasePassIndex, unsigned lightPassIndex);

    /// Invalidate pipeline state caches.
    virtual void InvalidatePipelineStateCache();
    /// Clear in the beginning of the frame.
    virtual void BeginFrame();
    /// Add source batch. Try to keep it non-virtual to optimize processing. Return whether it was lit.
    //bool AddSourceBatch(Drawable* drawable, unsigned sourceBatchIndex, Technique* technique);
    /// Collect scene batches.
    virtual void CollectSceneBatches(unsigned mainLightIndex, ea::span<LightProcessor*> sceneLights,
        const DrawableProcessor* drawableProcessor, Camera* camera, ScenePipelineStateCacheCallback& callback);
    /// Sort scene batches.
    virtual void SortSceneBatches() = 0;

protected:
    /// Collect unlit base batches.
    void CollectUnlitBatches(Camera* camera, ScenePipelineStateCacheCallback& callback);
    /// Collect lit base and light batches.
    void CollectLitBatches(Camera* camera, ScenePipelineStateCacheCallback& callback,
        unsigned mainLightIndex, ea::span<LightProcessor*> sceneLights, const DrawableProcessor* drawableProcessor);

    /// Sort batches (from vector).
    template <class T>
    static void SortBatches(const ea::vector<PipelineBatch>& sceneBatches, ea::vector<T>& sortedBatches)
    {
        const unsigned numBatches = sceneBatches.size();
        sortedBatches.resize(numBatches);
        for (unsigned i = 0; i < numBatches; ++i)
            sortedBatches[i] = T{ &sceneBatches[i] };
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

    /// Work queue.
    WorkQueue* const workQueue_{};
    /// Renderer.
    Renderer* const renderer_{};
    /// Number of worker threads.
    unsigned numThreads_{};

    /// Unlit base pass index.
    /*const unsigned unlitBasePassIndex_{};
    /// Lit base pass index.
    const unsigned litBasePassIndex_{};
    /// Additional light pass index.
    const unsigned lightPassIndex_{};*/
    /// Shader define for unlit base pass.
    const ea::string unlitBaseTag_;
    /// Shader define for lit base pass.
    const ea::string litBaseTag_;
    /// Shader define for light pass.
    const ea::string lightTag_;

    /// Unlit base scene batches.
    //ea::vector<PipelineBatch> unlitBaseBatches_;
    /// Lit base scene batches.
    //ea::vector<PipelineBatch> litBaseBatches_;
    /// Light scene batches.
    //WorkQueueVector<PipelineBatch> lightBatches_;

private:
    /// Unlit scene batches. Map to one base batch.
    /*WorkQueueVector<IntermediateSceneBatch> unlitBatches_;
    /// Lit intermediate batches. Always empty for Unlit passes.
    WorkQueueVector<IntermediateSceneBatch> litBatches_;*/

    /// Temporary vector to store unlit base batches without pipeline states.
    //WorkQueueVector<PipelineBatch*> unlitBaseBatchesDirty_;
    /// Temporary vector to store lit base batches without pipeline states.
    //WorkQueueVector<PipelineBatch*> litBaseBatchesDirty_;
    /// Temporary vector to store light batches without pipeline states.
    //WorkQueueVector<unsigned> lightBatchesDirty_;

    /// Pipeline state cache for unlit batches.
    //ScenePipelineStateCache unlitBasePipelineStateCache_;
    /// Pipeline state cache for lit batches.
    //ScenePipelineStateCache litBasePipelineStateCache_;
    /// Pipeline state cache for additional light batches.
    //ScenePipelineStateCache lightPipelineStateCache_;
};

/// Scene pass for forward lighting.
class URHO3D_API ForwardLightingScenePass : public ScenePass
{
    URHO3D_OBJECT(ForwardLightingScenePass, ScenePass);

public:
    /// Construct.
    ForwardLightingScenePass(RenderPipeline* renderPipeline, const DrawableProcessor* drawableProcessor, const ea::string& tag,
        const ea::string& unlitBasePass, const ea::string& litBasePass, const ea::string& lightPass);

private:
};

/// Scene pass for forward lighting (opaque objects).
class URHO3D_API OpaqueForwardLightingScenePass : public ForwardLightingScenePass
{
    URHO3D_OBJECT(OpaqueForwardLightingScenePass, ForwardLightingScenePass);

public:
    /// Construct.
    using ForwardLightingScenePass::ForwardLightingScenePass;

    /// Sort scene batches.
    virtual void SortSceneBatches() override;

    /// Return sorted unlit base batches.
    ea::span<const PipelineBatchByState> GetSortedUnlitBaseBatches() const { return sortedUnlitBaseBatches_; }
    /// Return sorted lit base batches.
    ea::span<const PipelineBatchByState> GetSortedLitBaseBatches() const { return sortedLitBaseBatches_; }
    /// Return sorted unlit base batches.
    ea::span<const PipelineBatchByState> GetSortedLightBatches() const { return sortedLightBatches_; }

protected:
    /// Sorted unlit base batches.
    ea::vector<PipelineBatchByState> sortedUnlitBaseBatches_;
    /// Sorted lit base batches.
    ea::vector<PipelineBatchByState> sortedLitBaseBatches_;
    /// Sorted light batches.
    ea::vector<PipelineBatchByState> sortedLightBatches_;
};

/// Scene pass for forward lighting (translucent objects).
class URHO3D_API AlphaForwardLightingScenePass : public ForwardLightingScenePass
{
    URHO3D_OBJECT(AlphaForwardLightingScenePass, ForwardLightingScenePass);

public:
    /// Construct.
    using ForwardLightingScenePass::ForwardLightingScenePass;

    /// Sort scene batches.
    virtual void SortSceneBatches() override;

    /// Return sorted unlit base batches.
    ea::span<const PipelineBatchBackToFront> GetSortedBatches() const { return sortedBatches_; }

protected:
    /// Sorted batches.
    ea::vector<PipelineBatchBackToFront> sortedBatches_;
};

/// Scene pass for unlit/deferred rendered objects.
class URHO3D_API UnlitScenePass : public ScenePass
{
    URHO3D_OBJECT(UnlitScenePass, ScenePass);

public:
    /// Construct.
    UnlitScenePass(RenderPipeline* renderPipeline, const DrawableProcessor* drawableProcessor, const ea::string& tag, const ea::string& pass);

    /// Sort scene batches.
    virtual void SortSceneBatches() override;

    /// Return sorted batches.
    ea::span<const PipelineBatchByState> GetBatches() const { return sortedBatches_; }

private:
    /// Sorted batches.
    ea::vector<PipelineBatchByState> sortedBatches_;
};

/// Scene pass for shadow rendering.
class URHO3D_API ShadowScenePass : public BatchCompositor
{
    URHO3D_OBJECT(ShadowScenePass, BatchCompositor);

public:
    /// Construct.
    ShadowScenePass(RenderPipeline* renderPipeline, const DrawableProcessor* drawableProcessor, const ea::string& tag, const ea::string& shadowPass);

    /// Invalidate pipeline state caches.
    virtual void InvalidatePipelineStateCache();
    /// Clear in the beginning of the frame.
    virtual void BeginFrame();
    /// Collect shadow batches for given light. Shall be safe to call from worker thread.
    virtual void CollectShadowBatches(MaterialQuality materialQuality, LightProcessor* sceneLight, unsigned splitIndex);
    /// Finalize shadow batches. Called from main thread.
    virtual void FinalizeShadowBatches(Camera* camera, ScenePipelineStateCacheCallback& callback);
    /// Sort and return shadow batches. Safe to call from worker thread.
    ea::span<const PipelineBatchByState> GetSortedShadowBatches(const ShadowSplitProcessor& split) const;

protected:
    /// Sort batches (from vector).
    template <class T>
    static void SortBatches(const ea::vector<PipelineBatch>& sceneBatches, ea::vector<T>& sortedBatches)
    {
        const unsigned numBatches = sceneBatches.size();
        sortedBatches.resize(numBatches);
        for (unsigned i = 0; i < numBatches; ++i)
            sortedBatches[i] = T{ &sceneBatches[i] };
        ea::sort(sortedBatches.begin(), sortedBatches.end());
    }

    /// Work queue.
    WorkQueue* const workQueue_{};
    /// Renderer.
    Renderer* const renderer_{};
    /// Number of worker threads.
    unsigned numThreads_{};

    /// Shader define tag.
    ea::string tag_;
    /// Shadow pass index.
    const unsigned shadowPassIndex_{};

    /// Temporary vector to store batches without pipeline states.
    //WorkQueueVector<ea::pair<ShadowSplitProcessor*, unsigned>> batchesDirty_;

    /// Pipeline state cache.
    //ScenePipelineStateCache pipelineStateCache_;
};

}
