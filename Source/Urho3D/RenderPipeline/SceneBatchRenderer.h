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
// TODO(renderer): We don't need this include
#include "../RenderPipeline/PipelineBatchSortKey.h"

#include <EASTL/span.h>

namespace Urho3D
{

class DrawableProcessor;

/// Geometry buffer texture for deferred renderer.
struct GeometryBufferResource
{
    /// Texture unit.
    TextureUnit unit_{};
    /// Texture resource.
    Texture* texture_{};
};

/// Utility class to convert batches into sequence of draw operations.
class SceneBatchRenderer : public Object
{
    URHO3D_OBJECT(SceneBatchRenderer, Object);

public:
    /// Construct.
    explicit SceneBatchRenderer(Context* context);

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

    /// Render light volume batches for deferred rendering.
    void RenderLightVolumeBatches(DrawCommandQueue& drawQueue, const DrawableProcessor& drawableProcessor,
        Camera* camera, Zone* zone, ea::span<const LightVolumeBatch> batches,
        ea::span<const GeometryBufferResource> geometryBuffer, const Vector4& geometryBufferOffset, const Vector2& geometryBufferInvSize);

private:
    /// Render generic batches.
    template <bool HasLight, class BatchType>
    void RenderBatches(DrawCommandQueue& drawQueue, const DrawableProcessor& drawableProcessor,
        Camera* camera, Zone* zone, ea::span<const BatchType> batches);

    /// Graphics subsystem.
    Graphics* graphics_{};
    /// Renderer subsystem.
    Renderer* renderer_{};

};

}
