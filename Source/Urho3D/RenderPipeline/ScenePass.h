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
#include "../RenderPipeline/PipelineBatchSortKey.h"
#include "../RenderPipeline/BatchCompositor.h"
#include "../RenderPipeline/DrawableProcessor.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace Urho3D
{

class RenderPipelineInterface;
class BatchRenderer;

/// Base type for scene pass.
class ScenePass : public BatchCompositorPass
{
    URHO3D_OBJECT(ScenePass, BatchCompositorPass);

public:
    /// Construct pass with forward lighting.
    ScenePass(RenderPipelineInterface* renderPipeline, DrawableProcessor* drawableProcessor,
        BatchStateCacheCallback* callback, DrawableProcessorPassFlags flags, const ea::string& deferredPass,
        const ea::string& unlitBasePass, const ea::string& litBasePass, const ea::string& lightPass);

    /// Construct pass without forward lighting.
    ScenePass(RenderPipelineInterface* renderPipeline, DrawableProcessor* drawableProcessor,
        BatchStateCacheCallback* callback, DrawableProcessorPassFlags flags, const ea::string& pass);

    /// Prepare instancing buffer for scene pass.
    virtual void PrepareInstacingBuffer(BatchRenderer* batchRenderer) = 0;
};

/// Scene pass with batches sorted by render order and pipeline state.
class URHO3D_API UnorderedScenePass : public ScenePass
{
    URHO3D_OBJECT(UnorderedScenePass, ScenePass);

public:
    using ScenePass::ScenePass;

    void PrepareInstacingBuffer(BatchRenderer* batchRenderer) override;

    PipelineBatchGroup<PipelineBatchByState>& GetDeferredBatches() { return deferredBatchGroup_; }
    PipelineBatchGroup<PipelineBatchByState>& GetBaseBatches() { return baseBatchGroup_; }
    PipelineBatchGroup<PipelineBatchByState>& GetLightBatches() { return lightBatchGroup_; }

protected:
    void OnBatchesReady() override;

    ea::vector<PipelineBatchByState> sortedDeferredBatches_;
    ea::vector<PipelineBatchByState> sortedBaseBatches_;
    ea::vector<PipelineBatchByState> sortedLightBatches_;

    PipelineBatchGroup<PipelineBatchByState> deferredBatchGroup_;
    PipelineBatchGroup<PipelineBatchByState> baseBatchGroup_;
    PipelineBatchGroup<PipelineBatchByState> lightBatchGroup_;
};

/// Scene pass with batches sorted by render order and distance back to front.
class URHO3D_API BackToFrontScenePass : public ScenePass
{
    URHO3D_OBJECT(BackToFrontScenePass, ScenePass);

public:
    using ScenePass::ScenePass;

    void PrepareInstacingBuffer(BatchRenderer* batchRenderer) override;

    PipelineBatchGroup<PipelineBatchBackToFront>& GetBatches() { return batchGroup_; }

    bool HasRefractionBatches() const { return hasRefractionBatches_; }

protected:
    void OnBatchesReady() override;

    ea::vector<PipelineBatchBackToFront> sortedBatches_;
    bool hasRefractionBatches_{};

    PipelineBatchGroup<PipelineBatchBackToFront> batchGroup_;
};

}
