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

#include "../Core/Context.h"
#include "../Core/Mutex.h"
#include "../Graphics/Drawable.h"
#include "../Graphics/DrawCommandQueue.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/PipelineState.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/RenderSurface.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/TextureCube.h"
#include "../Graphics/Viewport.h"
#include "../RenderPipeline/RenderPipelineInterface.h"
#include "../RenderPipeline/RenderBuffer.h"
#include "../IO/Log.h"

#include <EASTL/unordered_set.h>

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

/// Calculate optimal size for render target.
IntVector2 CalculateRenderTargetSize(const IntRect& viewportRect, const Vector2& sizeMultiplier, const IntVector2& explicitSize)
{
    if (explicitSize != IntVector2::ZERO)
        return explicitSize;
    const IntVector2 viewportSize = viewportRect.Size();
    return VectorMax(IntVector2::ONE, VectorRoundToInt(static_cast<Vector2>(viewportSize) * sizeMultiplier));
}

/// Return render surface of texture.
RenderSurface* GetRenderSurfaceFromTexture(Texture* texture, CubeMapFace face = FACE_POSITIVE_X)
{
    if (!texture)
        return nullptr;

    if (texture->GetType() == Texture2D::GetTypeStatic())
        return static_cast<Texture2D*>(texture)->GetRenderSurface();
    else if (texture->GetType() == TextureCube::GetTypeStatic())
        return static_cast<TextureCube*>(texture)->GetRenderSurface(face);
    else
        return nullptr;
}

}

Texture2D* RenderBuffer::GetTexture2D() const
{
    Texture* texture = GetTexture();
    return texture ? texture->Cast<Texture2D>() : nullptr;
}

RenderBuffer::RenderBuffer(RenderPipelineInterface* renderPipeline)
    : Object(renderPipeline->GetContext())
    , renderer_(GetSubsystem<Renderer>())
{
    renderPipeline->OnRenderBegin.Subscribe(this, &RenderBuffer::OnRenderBegin);
    renderPipeline->OnRenderEnd.Subscribe(this, &RenderBuffer::OnRenderEnd);
}

RenderBuffer::~RenderBuffer()
{
}

void RenderBuffer::OnRenderBegin(const FrameInfo& frameInfo)
{
    bufferIsReady_ = true;
}

void RenderBuffer::OnRenderEnd(const FrameInfo& frameInfo)
{
    bufferIsReady_ = false;
}

bool RenderBuffer::CheckIfBufferIsReady() const
{
    if (!bufferIsReady_)
    {
        URHO3D_LOGERROR("Cannot access RenderBuffer outside of RenderPipeline::Render");
        return false;
    }
    return true;
}

TextureRenderBuffer::TextureRenderBuffer(RenderPipelineInterface* renderPipeline,
    const RenderBufferParams& params, const Vector2& size)
    : RenderBuffer(renderPipeline)
    , params_(params)
{
    const bool fixedSize = params.flags_.Test(RenderBufferFlag::FixedTextureSize);
    if (fixedSize)
        fixedSize_ = VectorRoundToInt(size);
    else
        sizeMultiplier_ = size;

    const bool isPersistent = params.flags_.Test(RenderBufferFlag::Persistent);
    if (isPersistent)
        persistenceKey_ = GetObjectID();
}

TextureRenderBuffer::~TextureRenderBuffer()
{
}

void TextureRenderBuffer::OnRenderBegin(const FrameInfo& frameInfo)
{
    BaseClassName::OnRenderBegin(frameInfo);

    currentSize_ = CalculateRenderTargetSize(frameInfo.viewRect_, sizeMultiplier_, fixedSize_);
    const bool autoResolve = !params_.flags_.Test(RenderBufferFlag::NoMultiSampledAutoResolve);
    const bool isCubemap = params_.flags_.Test(RenderBufferFlag::CubeMap);
    const bool isFiltered = params_.flags_.Test(RenderBufferFlag::BilinearFiltering);
    const bool isSRGB = params_.flags_.Test(RenderBufferFlag::sRGB);

    currentTexture_ = renderer_->GetScreenBuffer(currentSize_.x_, currentSize_.y_,
        params_.textureFormat_, params_.multiSampleLevel_, autoResolve,
        isCubemap, isFiltered, isSRGB, persistenceKey_);
}

Texture* TextureRenderBuffer::GetTexture() const
{
    return CheckIfBufferIsReady() ? currentTexture_ : nullptr;
}

RenderSurface* TextureRenderBuffer::GetRenderSurface(CubeMapFace face) const
{
    return CheckIfBufferIsReady() ? GetRenderSurfaceFromTexture(currentTexture_, face) : nullptr;
}

IntRect TextureRenderBuffer::GetViewportRect() const
{
    return CheckIfBufferIsReady() ? IntRect{ IntVector2::ZERO, currentSize_ } : IntRect::ZERO;
}

ViewportColorRenderBuffer::ViewportColorRenderBuffer(RenderPipelineInterface* renderPipeline)
    : RenderBuffer(renderPipeline)
{
}

Texture* ViewportColorRenderBuffer::GetTexture() const
{
    return CheckIfBufferIsReady() ? renderTarget_->GetParentTexture() : nullptr;
}

RenderSurface* ViewportColorRenderBuffer::GetRenderSurface(CubeMapFace face) const
{
    return CheckIfBufferIsReady() ? renderTarget_ : nullptr;
}

void ViewportColorRenderBuffer::OnRenderBegin(const FrameInfo& frameInfo)
{
    BaseClassName::OnRenderBegin(frameInfo);
    renderTarget_ = frameInfo.renderTarget_;
    viewportRect_ = frameInfo.viewRect_;
}

ViewportDepthStencilRenderBuffer::ViewportDepthStencilRenderBuffer(RenderPipelineInterface* renderPipeline)
    : RenderBuffer(renderPipeline)
{
}

Texture* ViewportDepthStencilRenderBuffer::GetTexture() const
{
    return CheckIfBufferIsReady() && *depthStencil_ ? (*depthStencil_)->GetParentTexture() : nullptr;
}

RenderSurface* ViewportDepthStencilRenderBuffer::GetRenderSurface(CubeMapFace face) const
{
    return CheckIfBufferIsReady() ? *depthStencil_ : nullptr;
}

void ViewportDepthStencilRenderBuffer::OnRenderBegin(const FrameInfo& frameInfo)
{
    BaseClassName::OnRenderBegin(frameInfo);
    viewportRect_ = frameInfo.viewRect_;

    if (!frameInfo.renderTarget_)
    {
        // Backbuffer depth
        depthStencil_ = nullptr;
        bufferIsReady_ = true;
    }
    else if (auto depthStencil = frameInfo.renderTarget_->GetLinkedDepthStencil())
    {
        depthStencil_ = depthStencil;
        bufferIsReady_ = true;
    }
    else
    {
        depthStencil_ = ea::nullopt;
        bufferIsReady_ = false;
    }
}

}
