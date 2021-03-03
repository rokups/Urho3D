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
#include "../RenderPipeline/CommonTypes.h"

#include <EASTL/span.h>

namespace Urho3D
{

class Camera;
class DrawableProcessor;
class InstancingBuffer;
class ShadowSplitProcessor;
struct PipelineBatchBackToFront;
struct PipelineBatchByState;

/// Flags that control how exactly batches are rendered.
enum class BatchRenderFlag
{
    None = 0,
    EnableAmbientLighting = 1 << 0,
    EnableVertexLights = 1 << 1,
    EnablePixelLights = 1 << 2,
    EnableInstancingForStaticGeometry = 1 << 3,
};

URHO3D_FLAGSET(BatchRenderFlag, BatchRenderFlags);

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
    BatchRenderer(Context* context, const DrawableProcessor* drawableProcessor,
        InstancingBuffer* instancingBuffer);
    void SetSettings(const BatchRendererSettings& settings);

    /// Render batches
    /// @{
    void RenderBatches(const BatchRenderingContext& ctx,
        BatchRenderFlags flags, ea::span<const PipelineBatchByState> batches);
    void RenderBatches(const BatchRenderingContext& ctx,
        BatchRenderFlags flags, ea::span<const PipelineBatchBackToFront> batches);
    void RenderLightVolumeBatches(const BatchRenderingContext& ctx,
        ea::span<const PipelineBatchByState> batches);
    /// @}

private:
    /// External dependencies
    /// @{
    Renderer* renderer_{};
    const DrawableProcessor* drawableProcessor_{};
    InstancingBuffer* instancingBuffer_{};
    /// @}

    BatchRendererSettings settings_;
};

}
