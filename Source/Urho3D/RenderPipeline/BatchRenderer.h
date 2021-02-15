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
// TODO(renderer): We don't need this include
#include "../RenderPipeline/PipelineBatchSortKey.h"

#include <EASTL/span.h>

namespace Urho3D
{

class DrawCommandQueue;
class DrawableProcessor;

/// Batch rendering flags.
enum class BatchRenderFlag
{
    /// Default null flag.
    None = 0,
    /// Export ambient light and vertex lights.
    AmbientAndVertexLights = 1 << 0,
    /// Export pixel light.
    PixelLight = 1 << 1,
};

URHO3D_FLAGSET(BatchRenderFlag, BatchRenderFlags);

/// Shader resource binding (for global resources).
struct ShaderResourceDesc
{
    /// Texture unit.
    TextureUnit unit_{};
    /// Texture resource.
    Texture* texture_{};
};

/// Light volume batch rendering context.
struct LightVolumeRenderContext
{
    /// Geometry buffer resources.
    ea::span<const ShaderResourceDesc> geometryBuffer_;
    /// Geometry buffer offset and scale.
    Vector4 geometryBufferOffsetAndScale_;
    /// Geometry buffer inverse scale.
    Vector2 geometryBufferInvSize_;
};

/// Utility class to convert pipeline batches into sequence of draw commands.
class URHO3D_API BatchRenderer : public Object
{
    URHO3D_OBJECT(BatchRenderer, Object);

public:
    /// Construct.
    BatchRenderer(Context* context, const DrawableProcessor* drawableProcessor);

    /// Render batches (sorted by state).
    void RenderBatches(DrawCommandQueue& drawQueue, const Camera* camera, BatchRenderFlags flags,
        ea::span<const PipelineBatchByState> batches);
    /// Render batches (sorted by distance).
    void RenderBatches(DrawCommandQueue& drawQueue, const Camera* camera, BatchRenderFlags flags,
        ea::span<const PipelineBatchBackToFront> batches);

    /// Render light volume batches for deferred rendering.
    void RenderLightVolumeBatches(DrawCommandQueue& drawQueue, Camera* camera,
        const LightVolumeRenderContext& ctx, ea::span<const PipelineBatchByState> batches);

    /// Render unlit base batches. Safe to call from worker thread.
    void RenderUnlitBaseBatches(DrawCommandQueue& drawQueue, const DrawableProcessor& drawableProcessor,
        Camera* camera, Zone* zone, ea::span<const PipelineBatchByState> batches);
    /// Render lit base batches. Safe to call from worker thread.
    void RenderLitBaseBatches(DrawCommandQueue& drawQueue, const DrawableProcessor& drawableProcessor,
        Camera* camera, Zone* zone, ea::span<const PipelineBatchByState> batches);
    /// Render light batches. Safe to call from worker thread.
    //void RenderLightBatches(DrawCommandQueue& drawQueue, const DrawableProcessor& drawableProcessor,
    //    Camera* camera, Zone* zone, ea::span<const LightBatchSortedByState> batches);
    /// Render unlit and lit alpha batches. Safe to call from worker thread.
    void RenderAlphaBatches(DrawCommandQueue& drawQueue, const DrawableProcessor& drawableProcessor,
        Camera* camera, Zone* zone, ea::span<const PipelineBatchBackToFront> batches);
    /// Render shadow batches. Safe to call from worker thread.
    void RenderShadowBatches(DrawCommandQueue& drawQueue, const DrawableProcessor& drawableProcessor,
        Camera* camera, Zone* zone, ea::span<const PipelineBatchByState> batches);

private:
    /// Render generic batches.
    template <bool HasLight, class BatchType>
    void RenderBatches(DrawCommandQueue& drawQueue, const DrawableProcessor& drawableProcessor,
        Camera* camera, Zone* zone, ea::span<const BatchType> batches);

    /// Graphics subsystem.
    Graphics* graphics_{};
    /// Renderer subsystem.
    Renderer* renderer_{};

    /// Drawable processor.
    const DrawableProcessor* drawableProcessor_{};
};

}
