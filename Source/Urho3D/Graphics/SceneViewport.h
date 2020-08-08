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

#include "../Core/Context.h"
#include "../Core/Object.h"
#include "../Graphics/Camera.h"
#include "../Graphics/GraphicsDefs.h"
#include "../Graphics/PipelineStateTracker.h"
#include "../Graphics/RenderSurface.h"
#include "../Graphics/Viewport.h"

namespace Urho3D
{

/// Global state of scene viewport.
class SceneViewport : public Object, public PipelineStateTracker
{
    URHO3D_OBJECT(SceneViewport, Object);

public:
    /// Construct.
    explicit SceneViewport(Context* context);

    /// Begin frame.
    void BeginFrame(RenderSurface* renderTarget, Viewport* viewport);
    /// Return whether the pipeline state caches shall be invalidated.
    bool ArePipelineStatesInvalidated() const { return GetPipelineStateHash() != cachedPipelineStateHash_; }
    /// Return output viewport rectange.
    IntRect GetViewportRect() const { return viewportRect_; }
    /// Enable output render target with given depth stencil (nullptr to use default depth stencil).
    void SetOutputRenderTarget(RenderSurface* depthStencil = nullptr);
    /// End frame.
    void EndFrame();

protected:
    /// Mark pipeline state hash as dirty.
    unsigned RecalculatePipelineStateHash() const override;

private:
    /// Graphics subsystem.
    Graphics* graphics_{};
    /// Renderer subsystem.
    Renderer* renderer_{};

    /// Viewport.
    Viewport* viewport_{};
    /// Render target.
    RenderSurface* renderTarget_{};
    /// Viewport rectangle.
    IntRect viewportRect_;

    /// Cached pipeline state hash from previous frame.
    unsigned cachedPipelineStateHash_{};
    /// Cull camera.
    Camera* cullCamera_{};
    /// Whether the constant buffers are enabled.
    bool constantBuffersEnabled_{};
};

}
