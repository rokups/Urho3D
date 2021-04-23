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
#include "../Graphics/DrawCommandQueue.h"
#include "../Graphics/GraphicsDefs.h"
#include "../RenderPipeline/RenderPipelineDefs.h"
#include "../RenderPipeline/PipelineBatchSortKey.h"

#include <EASTL/span.h>

namespace Urho3D
{

class Camera;
class DrawableProcessor;
class InstancingBuffer;
class ShadowSplitProcessor;

/// Common parameters of batch rendering
struct BatchRenderingContext
{
    DrawCommandQueue& drawQueue_;
    const Camera& camera_;
    const ShadowSplitProcessor* outputShadowSplit_{};

    ea::span<const ShaderResourceDesc> globalResources_;
    ea::span<const ShaderParameterDesc> frameParameters_;
    ea::span<const ShaderParameterDesc> cameraParameters_;

    BatchRenderingContext(DrawCommandQueue& drawQueue, const Camera& camera);
    BatchRenderingContext(DrawCommandQueue& drawQueue, const ShadowSplitProcessor& outputShadowSplit);
};

/// Utility class to convert pipeline batches into sequence of draw commands.
class URHO3D_API BatchRenderer : public Object
{
    URHO3D_OBJECT(BatchRenderer, Object);

public:
    BatchRenderer(RenderPipelineInterface* renderPipeline, const DrawableProcessor* drawableProcessor,
        InstancingBuffer* instancingBuffer);
    void SetSettings(const BatchRendererSettings& settings);

    /// Render batches
    /// @{
    void RenderBatches(const BatchRenderingContext& ctx, PipelineBatchGroup<PipelineBatchByState> batchGroup);
    void RenderBatches(const BatchRenderingContext& ctx, PipelineBatchGroup<PipelineBatchBackToFront> batchGroup);
    void RenderLightVolumeBatches(const BatchRenderingContext& ctx,
        ea::span<const PipelineBatchByState> batches);
    /// @}

    /// Store instancing data for batches.
    /// @{
    void PrepareInstancingBuffer(PipelineBatchGroup<PipelineBatchByState>& batches);
    void PrepareInstancingBuffer(PipelineBatchGroup<PipelineBatchBackToFront>& batches);
    /// @}

private:
    template <class T>
    void PrepareInstancingBufferImpl(PipelineBatchGroup<T>& batches);
    BatchRenderFlags AdjustRenderFlags(BatchRenderFlags flags) const;

    /// External dependencies
    /// @{
    Renderer* renderer_{};
    RenderPipelineDebugger* debugger_{};
    const DrawableProcessor* drawableProcessor_{};
    InstancingBuffer* instancingBuffer_{};
    /// @}

    BatchRendererSettings settings_;
};

}
