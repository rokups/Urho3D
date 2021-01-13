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

#include "../Graphics/Drawable.h"
#include "../RenderPipeline/RenderPipeline.h"
#include "../RenderPipeline/RenderPipelineCamera.h"

#include "../DebugNew.h"

namespace Urho3D
{

RenderPipelineCamera::RenderPipelineCamera(RenderPipeline* renderPipeline)
    : Object(renderPipeline->GetContext())
{
    renderPipeline->OnRenderBegin.Subscribe(this, &RenderPipelineCamera::OnRenderBegin);
    renderPipeline->OnRenderEnd.Subscribe(this, &RenderPipelineCamera::OnRenderEnd);
}

void RenderPipelineCamera::Initialize(Camera* camera)
{
    camera_ = camera;
}

void RenderPipelineCamera::OnRenderBegin(const FrameInfo& frameInfo)
{
    flipCamera_ = false;

#ifdef URHO3D_OPENGL
    // On OpenGL, flip the projection if rendering to a texture so that the texture can be addressed in the same way
    // as a render texture produced on Direct3D
    if (frameInfo.renderTarget_)
        flipCamera_ = true;
#endif

    MarkPipelineStateHashDirty();

    if (camera_)
    {
        if (flipCamera_)
            camera_->SetFlipVertical(!camera_->GetFlipVertical());

        if (camera_->GetAutoAspectRatio())
            camera_->SetAspectRatioInternal(static_cast<float>(frameInfo.viewSize_.x_) / frameInfo.viewSize_.y_);
    }
}

void RenderPipelineCamera::OnRenderEnd(const FrameInfo& frameInfo)
{
    if (flipCamera_ && camera_)
        camera_->SetFlipVertical(!camera_->GetFlipVertical());
}

unsigned RenderPipelineCamera::RecalculatePipelineStateHash() const
{
    unsigned hash = 0;
    if (camera_)
        CombineHash(hash, camera_->GetFlipVertical());
    return hash;
}

}
