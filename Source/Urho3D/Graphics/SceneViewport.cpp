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

#include "../Precompiled.h"

#include "../Graphics/Graphics.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/SceneViewport.h"

#include "../DebugNew.h"

namespace Urho3D
{

SceneViewport::SceneViewport(Context* context)
    : Object(context)
    , graphics_(context_->GetGraphics())
    , renderer_(context_->GetRenderer())
{}

void SceneViewport::BeginFrame(RenderSurface* renderTarget, Viewport* viewport)
{
    viewport_ = viewport;
    renderTarget_ = renderTarget;

    // Update viewport rect
    if (viewport_->GetRect() != IntRect::ZERO)
        viewportRect_ = viewport_->GetRect();
    else
        viewportRect_ = { IntVector2::ZERO, graphics_->GetRenderTargetDimensions() };

    // Update pipeline state inputs
    cullCamera_ = viewport->GetCamera();
    constantBuffersEnabled_ = graphics_->GetConstantBuffersEnabled();
    MarkPipelineStateHashDirty();

#ifdef URHO3D_OPENGL
    // On OpenGL, flip the projection if rendering to a texture so that the texture can be addressed in the same way
    // as a render texture produced on Direct3D
    if (renderTarget_)
    {
        if (cullCamera_)
            cullCamera_->SetFlipVertical(!cullCamera_->GetFlipVertical());
    }
#endif
}

void SceneViewport::SetOutputRenderTarget(RenderSurface* depthStencil)
{
    graphics_->SetRenderTarget(0, renderTarget_);
    graphics_->SetDepthStencil(depthStencil);
    graphics_->SetViewport(viewportRect_);
}

void SceneViewport::EndFrame()
{
    cachedPipelineStateHash_ = GetPipelineStateHash();

#ifdef URHO3D_OPENGL
    // Undo flip
    if (renderTarget_)
    {
        if (cullCamera_)
            cullCamera_->SetFlipVertical(!cullCamera_->GetFlipVertical());
    }
#endif
}

unsigned SceneViewport::RecalculatePipelineStateHash() const
{
    unsigned hash = 0;
    CombineHash(hash, cullCamera_->GetFlipVertical());
    CombineHash(hash, constantBuffersEnabled_);
    return hash;
}

}
