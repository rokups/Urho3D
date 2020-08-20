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
#include "../Core/ThreadedVector.h"
#include "../Graphics/DrawableLightAccumulator.h"
#include "../Graphics/SceneBatch.h"
#include "../Graphics/SceneLight.h"
#include "../Graphics/ScenePipelineStateCache.h"
/*
#include "../Graphics/Light.h"
#include "../Graphics/SceneDrawableData.h"
#include "../Graphics/ShadowMapAllocator.h"
#include "../Graphics/PipelineStateTracker.h"
#include "../Graphics/Camera.h"
#include "../Scene/Node.h"*/

#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace Urho3D
{

class Renderer;
class WorkQueue;

/// Scene pass interface.
class ScenePass : public Object
{
    URHO3D_OBJECT(ScenePass, Object);

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
    ScenePass(Context* context,
        const ea::string& unlitBaseTag, const ea::string& litBaseTag, const ea::string& lightTag,
        unsigned unlitBasePassIndex, unsigned litBasePassIndex, unsigned lightPassIndex);

    /// Clear in the beginning of the frame.
    virtual void BeginFrame();
    /// Add source batch. Try to keep it non-virtual to optimize processing. Return whether it was lit.
    bool AddSourceBatch(Drawable* drawable, unsigned sourceBatchIndex, Technique* technique);
    /// Collect scene batches.
    virtual void CollectSceneBatches(unsigned mainLightIndex, ea::span<SceneLight*> sceneLights,
        const DrawableLightingData& drawableLighting, Camera* camera, ScenePipelineStateCacheCallback& callback);
    /// Sort scene batches.
    virtual void SortSceneBatches() = 0;

protected:
    /// Collect unlit base batches.
    void CollectUnlitBatches(Camera* camera, ScenePipelineStateCacheCallback& callback);
    /// Collect lit base and light batches.
    void CollectLitBatches(Camera* camera, ScenePipelineStateCacheCallback& callback,
        unsigned mainLightIndex, ea::span<SceneLight*> sceneLights, const DrawableLightingData& drawableLighting);

    /// Sort batches (from vector).
    template <class T>
    static void SortBatches(const ea::vector<BaseSceneBatch>& sceneBatches, ea::vector<T>& sortedBatches)
    {
        const unsigned numBatches = sceneBatches.size();
        sortedBatches.resize(numBatches);
        for (unsigned i = 0; i < numBatches; ++i)
            sortedBatches[i] = T{ &sceneBatches[i] };
        ea::sort(sortedBatches.begin(), sortedBatches.end());
    }

    /// Sort batches (from ThreadedVector).
    template <class T>
    static void SortBatches(const ThreadedVector<BaseSceneBatch>& sceneBatches, ea::vector<T>& sortedBatches)
    {
        const unsigned numBatches = sceneBatches.Size();
        sortedBatches.resize(numBatches);
        sceneBatches.ForEach([&](unsigned, unsigned elementIndex, const BaseSceneBatch& lightBatch)
        {
            sortedBatches[elementIndex] = T{ &lightBatch };
        });
        ea::sort(sortedBatches.begin(), sortedBatches.end());
    }

    /// Work queue.
    WorkQueue* const workQueue_{};
    /// Renderer.
    Renderer* const renderer_{};
    /// Number of worker threads.
    unsigned numThreads_{};

    /// Unlit base pass index.
    const unsigned unlitBasePassIndex_{};
    /// Lit base pass index.
    const unsigned litBasePassIndex_{};
    /// Additional light pass index.
    const unsigned lightPassIndex_{};
    /// Shader define for unlit base pass.
    const ea::string unlitBaseTag_;
    /// Shader define for lit base pass.
    const ea::string litBaseTag_;
    /// Shader define for light pass.
    const ea::string lightTag_;

    /// Unlit base scene batches.
    ea::vector<BaseSceneBatch> unlitBaseBatches_;
    /// Lit base scene batches.
    ea::vector<BaseSceneBatch> litBaseBatches_;
    /// Light scene batches.
    ThreadedVector<BaseSceneBatch> lightBatches_;

private:
    /// Unlit scene batches. Map to one base batch.
    ThreadedVector<IntermediateSceneBatch> unlitBatches_;
    /// Lit intermediate batches. Always empty for Unlit passes.
    ThreadedVector<IntermediateSceneBatch> litBatches_;

    /// Temporary vector to store unlit base batches without pipeline states.
    ThreadedVector<BaseSceneBatch*> unlitBaseBatchesDirty_;
    /// Temporary vector to store lit base batches without pipeline states.
    ThreadedVector<BaseSceneBatch*> litBaseBatchesDirty_;
    /// Temporary vector to store light batches without pipeline states.
    ThreadedVector<unsigned> lightBatchesDirty_;

    /// Pipeline state cache for unlit batches.
    ScenePipelineStateCache unlitPipelineStateCache_;
    /// Pipeline state cache for lit batches.
    ScenePipelineStateCache litPipelineStateCache_;
    /// Pipeline state cache for additional light batches.
    ScenePipelineStateCache additionalLightPipelineStateCache_;
};

/// Scene pass for forward lighting.
class ForwardLightingScenePass : public ScenePass
{
    URHO3D_OBJECT(ForwardLightingScenePass, ScenePass);

public:
    /// Construct.
    ForwardLightingScenePass(Context* context, const ea::string& tag,
        const ea::string& unlitBasePass, const ea::string& litBasePass, const ea::string& lightPass);

private:
};

/// Scene pass for forward lighting (opaque objects).
class OpaqueForwardLightingScenePass : public ForwardLightingScenePass
{
    URHO3D_OBJECT(OpaqueForwardLightingScenePass, ForwardLightingScenePass);

public:
    /// Construct.
    using ForwardLightingScenePass::ForwardLightingScenePass;

    /// Sort scene batches.
    virtual void SortSceneBatches() override;

    /// Return sorted unlit base batches.
    ea::span<const BaseSceneBatchSortedByState> GetSortedUnlitBaseBatches() const { return sortedUnlitBaseBatches_; }
    /// Return sorted lit base batches.
    ea::span<const BaseSceneBatchSortedByState> GetSortedLitBaseBatches() const { return sortedLitBaseBatches_; }
    /// Return sorted unlit base batches.
    ea::span<const LightBatchSortedByState> GetSortedLightBatches() const { return sortedLightBatches_; }

protected:
    /// Sorted unlit base batches.
    ea::vector<BaseSceneBatchSortedByState> sortedUnlitBaseBatches_;
    /// Sorted lit base batches.
    ea::vector<BaseSceneBatchSortedByState> sortedLitBaseBatches_;
    /// Sorted light batches.
    ea::vector<LightBatchSortedByState> sortedLightBatches_;
};

/// Scene pass for shadow rendering.
class ShadowScenePass : public Object
{
    URHO3D_OBJECT(ShadowScenePass, Object);

public:
    /// Construct.
    ShadowScenePass(Context* context, const ea::string& tag, const ea::string& shadowPass);

    /// Clear in the beginning of the frame.
    virtual void BeginFrame();
    /// Collect shadow batches for given light. Shall be safe to call from worker thread.
    virtual void CollectShadowBatches(MaterialQuality materialQuality, SceneLight* sceneLight, unsigned splitIndex);
    /// Finalize shadow batches. Called from main thread.
    virtual void FinalizeShadowBatches(Camera* camera, ScenePipelineStateCacheCallback& callback);
    /// Sort and return shadow batches. Safe to call from worker thread.
    ea::span<const BaseSceneBatchSortedByState> GetSortedShadowBatches(const SceneLightShadowSplit& split) const;

protected:
    /// Sort batches (from vector).
    template <class T>
    static void SortBatches(const ea::vector<BaseSceneBatch>& sceneBatches, ea::vector<T>& sortedBatches)
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
    ThreadedVector<ea::pair<SceneLightShadowSplit*, unsigned>> batchesDirty_;

    /// Pipeline state cache.
    ScenePipelineStateCache pipelineStateCache_;
};

}
