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
#include "../Graphics/GraphicsDefs.h"

#include <EASTL/span.h>

namespace Urho3D
{

class DrawCommandQueue;
class PipelineState;
class RenderSurface;
class RenderPipeline;
class Texture;
class Viewport;
struct FrameInfo;

/// Writable texture or texture region used during RenderPipeline execution. Readability is not guaranteed.
class URHO3D_API RenderPipelineTexture : public Object
{
    URHO3D_OBJECT(RenderPipelineTexture, Object);

public:
    /// Return whether this texture is compatible with another, i.e. can be set together.
    bool IsCompatibleWith(RenderPipelineTexture* otherTexture) const;

    /// Clear as color texture. No-op for depth-stencil texture.
    virtual void ClearColor(const Color& color = Color::TRANSPARENT_BLACK, CubeMapFace face = FACE_POSITIVE_X);

    /// Clear as depth-stencil texture. No-op for color texture.
    virtual void ClearDepthStencil(float depth = 1.0f, unsigned stencil = 0, CubeMapFace face = FACE_POSITIVE_X);

    /// Set subregion of multiple render targets treating this texture as depth-stencil.
    void SetRenderTargetsRegion(const IntRect& viewportRect,
        ea::span<RenderPipelineTexture* const> colorTextures, CubeMapFace face = FACE_POSITIVE_X);

    /// Set multiple render targets treating this texture as depth-stencil.
    void SetRenderTargets(ea::span<RenderPipelineTexture* const> colorTextures, CubeMapFace face = FACE_POSITIVE_X);

    /// Set subregion of multiple render targets treating this texture as depth-stencil.
    void SetRenderTargetsRegion(const IntRect& viewportRect,
        std::initializer_list<RenderPipelineTexture*> colorTextures, CubeMapFace face = FACE_POSITIVE_X);

    /// Set multiple render targets treating this texture as depth-stencil.
    void SetRenderTargets(std::initializer_list<RenderPipelineTexture*> colorTextures, CubeMapFace face = FACE_POSITIVE_X);

    /// Copy contents from another texture subregion with optional vertical flip.
    void CopyRegionFrom(Texture* sourceTexture, const IntRect& sourceViewportRect,
        CubeMapFace destinationFace, const IntRect& destinationViewportRect, bool flipVertical);

    /// Copy contents from another texture into region.
    void CopyFrom(RenderPipelineTexture* texture,
        CubeMapFace destinationFace, const IntRect& destinationViewportRect, bool flipVertical);

    /// Copy contents from another texture into this texture.
    void CopyFrom(RenderPipelineTexture* texture, bool flipVertical = false);

    /// Return size of the texture.
    IntVector2 GetSize() const;

    /// Return offset and scale of specified viewport within this texture.
    /// If viewport is not specified, whole texture is used.
    Vector4 GetViewportOffsetAndScale(const IntRect& viewportRect = IntRect::ZERO) const;

    /// Return inverted size of this texture.
    Vector2 GetInvSize() const;

    /// Return readable texture.
    virtual Texture* GetTexture() const = 0;

    /// Return render surface. Face could be specified for cubemap texture.
    virtual RenderSurface* GetRenderSurface(CubeMapFace face = FACE_POSITIVE_X) const = 0;

    /// Return effective viewport rectangle. Always equal to whole texture for screen buffers, not so for viewport.
    virtual IntRect GetViewportRect() const = 0;

protected:
    /// Construct.
    RenderPipelineTexture(RenderPipeline* renderPipeline);
    /// Destruct.
    ~RenderPipelineTexture() override;
    /// Called when render begins.
    virtual void OnRenderBegin(const FrameInfo& frameInfo);
    /// Called when render ends.
    virtual void OnRenderEnd(const FrameInfo& frameInfo);
    /// Bind render surface to graphics pipeline.
    void SetRenderTarget(RenderSurface* renderSurface);
    /// Check whether the rendering in progress.
    bool CheckRendering() const;

protected:
    /// Cached pointer to Renderer.
    Renderer* renderer_{};
    /// Cached pointer to Graphics.
    Graphics* graphics_{};

private:
    /// Draw queue used for texture copying.
    SharedPtr<DrawCommandQueue> drawQueue_;
    /// Pipeline state used to copy texture.
    SharedPtr<PipelineState> copyPipelineState_;
    /// Whether the pipeline state is for constant buffer mode.
    bool currentConstantBuffersEnabled_{};
    /// Whether the rendering is active and texture is valid.
    bool active_{};
};

/// Screen buffer creation parameters.
struct ScreenBufferParams
{
    /// Texture format.
    unsigned format_{};
    /// Whether is sRGB (if supported).
    bool sRGB_{};
    /// Whether the render target is cubemap.
    bool cubemap_{};
    /// Whether to use bilinear filtering.
    bool filtered_{};
    /// Multisample level.
    int multiSample_{ 1 };
    /// Whether to automatically resolve multisampled texture.
    bool autoResolve_{ true };
};

/// Writable and readable screen buffer texture (2D or cubemap).
class URHO3D_API ScreenBufferTexture : public RenderPipelineTexture
{
    URHO3D_OBJECT(ScreenBufferTexture, RenderPipelineTexture);

public:
    /// Construct.
    ScreenBufferTexture(RenderPipeline* renderPipeline, const ScreenBufferParams& params,
        const Vector2& sizeMultiplier, const IntVector2& fixedSize, bool persistent);
    /// Destruct.
    ~ScreenBufferTexture() override;

    /// Return readable texture.
    Texture* GetTexture() const override;

    /// Return render surface. Face could be specified for cubemap texture.
    RenderSurface* GetRenderSurface(CubeMapFace face = FACE_POSITIVE_X) const override;

    /// Return effective viewport rectangle. Always equal to whole texture.
    IntRect GetViewportRect() const override;

protected:
    /// Called when render begins.
    void OnRenderBegin(const FrameInfo& frameInfo) override;

    /// Texture parameters.
    ScreenBufferParams params_;
    /// Size multiplier.
    Vector2 sizeMultiplier_ = Vector2::ONE;
    /// Fixed size. If zero, ignored.
    IntVector2 fixedSize_;
    /// Persistent key. If not zero, texture will not be reused.
    unsigned persistenceKey_{};

    /// Current size.
    IntVector2 currentSize_;
    /// Current texture.
    Texture* currentTexture_{};
};

/// Optionally write-only viewport color texture.
class URHO3D_API ViewportColorTexture : public RenderPipelineTexture
{
    URHO3D_OBJECT(ViewportColorTexture, RenderPipelineTexture);

public:
    /// Construct.
    ViewportColorTexture(RenderPipeline* renderPipeline);

    /// Not supported.
    void ClearDepthStencil(float depth, unsigned stencil, CubeMapFace face) override;

    /// Return readable texture.
    Texture* GetTexture() const override;

    /// Return render surface. Face could be specified for cubemap texture.
    RenderSurface* GetRenderSurface(CubeMapFace face = FACE_POSITIVE_X) const override;

    /// Return effective viewport rectangle.
    IntRect GetViewportRect() const override { return CheckRendering() ? viewportRect_ : IntRect::ZERO; }

protected:
    /// Called when render begins.
    void OnRenderBegin(const FrameInfo& frameInfo) override;

private:
    /// Viewport rectangle used.
    IntRect viewportRect_{};
    /// Render target. Null if rendering to backbuffer.
    RenderSurface* renderTarget_{};
};

/// Optionally write-only viewport depth-stenil texture.
class URHO3D_API ViewportDepthStencilTexture : public RenderPipelineTexture
{
    URHO3D_OBJECT(ViewportDepthStencilTexture, RenderPipelineTexture);

public:
    /// Construct.
    ViewportDepthStencilTexture(RenderPipeline* renderPipeline);

    /// Clear as color texture. No-op for depth-stencil texture.
    void ClearColor(const Color& color, CubeMapFace face) override;

    /// Return readable texture.
    Texture* GetTexture() const override;

    /// Return render surface. Face could be specified for cubemap texture.
    RenderSurface* GetRenderSurface(CubeMapFace face = FACE_POSITIVE_X) const override;

    /// Return effective viewport rectangle.
    IntRect GetViewportRect() const override { return CheckRendering() ? viewportRect_ : IntRect::ZERO; }

protected:
    /// Called when render begins.
    void OnRenderBegin(const FrameInfo& frameInfo) override;

private:
    /// Viewport rectangle used.
    IntRect viewportRect_{};
    /// Render target. Null if rendering to backbuffer.
    RenderSurface* renderTarget_{};
};

}
