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
#include "../RenderPipeline/PipelineBatchSortKey.h"
#include "../RenderPipeline/LightProcessor.h"
#include "../RenderPipeline/BatchStateCache.h"
#include "../RenderPipeline/BatchCompositor.h"
#include "../RenderPipeline/DrawableProcessor.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/sort.h>

namespace Urho3D
{

/// Scene pass for forward lighting.
class URHO3D_API ForwardLightingScenePass : public BatchCompositorPass
{
    URHO3D_OBJECT(ForwardLightingScenePass, BatchCompositorPass);

public:
    /// Construct.
    ForwardLightingScenePass(RenderPipelineInterface* renderPipeline, DrawableProcessor* drawableProcessor,
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
    virtual void OnBatchesReady() override;

    /// Return sorted lit base batches.
    ea::span<const PipelineBatchByState> GetSortedBaseBatches() const { return sortedBaseBatches_; }
    /// Return sorted unlit base batches.
    ea::span<const PipelineBatchByState> GetSortedLightBatches() const { return sortedLightBatches_; }

protected:
    /// Sorted lit base batches.
    ea::vector<PipelineBatchByState> sortedBaseBatches_;
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
    virtual void OnBatchesReady() override;

    /// Return sorted unlit base batches.
    ea::span<const PipelineBatchBackToFront> GetSortedBatches() const { return sortedBatches_; }

protected:
    /// Sorted batches.
    ea::vector<PipelineBatchBackToFront> sortedBatches_;
};

/// Scene pass for unlit/deferred rendered objects.
class URHO3D_API UnlitScenePass : public BatchCompositorPass
{
    URHO3D_OBJECT(UnlitScenePass, BatchCompositorPass);

public:
    /// Construct.
    UnlitScenePass(RenderPipelineInterface* renderPipeline, DrawableProcessor* drawableProcessor, const ea::string& pass);

    /// Sort scene batches.
    virtual void OnBatchesReady() override;

    /// Return sorted batches.
    ea::span<const PipelineBatchByState> GetBatches() const { return sortedBatches_; }

private:
    /// Sorted batches.
    ea::vector<PipelineBatchByState> sortedBatches_;
};

}
