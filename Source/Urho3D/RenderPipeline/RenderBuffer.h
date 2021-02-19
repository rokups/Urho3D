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

#include "../Container/FlagSet.h"
#include "../Container/IndexAllocator.h"
#include "../Core/Object.h"
#include "../Graphics/GraphicsDefs.h"

#include <EASTL/span.h>

namespace Urho3D
{

class DrawCommandQueue;
class PipelineState;
class RenderSurface;
class RenderPipelineInterface;
class Texture;
class Viewport;
struct FrameInfo;

/// Render buffer easy creation flags.
enum class RenderBufferFlag
{
    /// Default flag: color buffer.
    RGBA = 0,
    /// Render buffer contains depth data.
    Depth = 1 << 1,
    /// Render buffer contains stencil data. Depth data is assumed.
    Stencil = 1 << 2,
    /// Texture content is preserved between frames.
    Persistent = 1 << 3,
    /// Texture cannot be read in shader.
    WriteOnly = 1 << 4,
    /// Use fixed texture size in pixels instead of relative size to viewport.
    FixedSize = 1 << 5,
    /// sRGB texture format is used.
    sRGB = 1 << 6,
    /// Bilinear filtering is used.
    Filter = 1 << 7,
    /// Render buffer is cubemap.
    CubeMap = 1 << 8,
};

URHO3D_FLAGSET(RenderBufferFlag, RenderBufferFlags);

/// Base class fro writable texture or texture region. Readability is not guaranteed.
class URHO3D_API RenderBuffer : public Object, public IDFamily<RenderBuffer>
{
    URHO3D_OBJECT(RenderBuffer, Object);

public:
    /// Create texture render buffer.
    static SharedPtr<RenderBuffer> Create(RenderPipelineInterface* renderPipeline,
        RenderBufferFlags flags, const Vector2& size = Vector2::ONE, int multiSample = 1);
    /// Create render buffer that connects to viewport color.
    static SharedPtr<RenderBuffer> ConnectToViewportColor(RenderPipelineInterface* renderPipeline);
    /// Create render buffer that connects to viewport depth-stencil.
    static SharedPtr<RenderBuffer> ConnectToViewportDepthStencil(RenderPipelineInterface* renderPipeline);

    /// Return whether this texture is compatible with another, i.e. can be set together.
    bool IsCompatibleWith(RenderBuffer* otherTexture) const;

    /// Clear as color texture. No-op for depth-stencil texture.
    virtual void ClearColor(const Color& color = Color::TRANSPARENT_BLACK, CubeMapFace face = FACE_POSITIVE_X);

    /// Clear as depth-stencil texture. No-op for color texture.
    virtual void ClearDepthStencil(float depth = 1.0f, unsigned stencil = 0, CubeMapFace face = FACE_POSITIVE_X);

    /// Set subregion of multiple render targets treating this texture as depth-stencil.
    void SetRenderTargetsRegion(const IntRect& viewportRect,
        ea::span<RenderBuffer* const> colorTextures, CubeMapFace face = FACE_POSITIVE_X);

    /// Set multiple render targets treating this texture as depth-stencil.
    void SetRenderTargets(ea::span<RenderBuffer* const> colorTextures, CubeMapFace face = FACE_POSITIVE_X);

    /// Set subregion of multiple render targets treating this texture as depth-stencil.
    void SetRenderTargetsRegion(const IntRect& viewportRect,
        std::initializer_list<RenderBuffer*> colorTextures, CubeMapFace face = FACE_POSITIVE_X);

    /// Set multiple render targets treating this texture as depth-stencil.
    void SetRenderTargets(std::initializer_list<RenderBuffer*> colorTextures, CubeMapFace face = FACE_POSITIVE_X);

    /// Copy contents from another texture subregion with optional vertical flip.
    void CopyRegionFrom(Texture* sourceTexture, const IntRect& sourceViewportRect,
        CubeMapFace destinationFace, const IntRect& destinationViewportRect, bool flipVertical);

    /// Copy contents from another texture into region.
    void CopyFrom(RenderBuffer* texture,
        CubeMapFace destinationFace, const IntRect& destinationViewportRect, bool flipVertical);

    /// Copy contents from another texture into this texture.
    void CopyFrom(RenderBuffer* texture, bool flipVertical = false);

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
    RenderBuffer(RenderPipelineInterface* renderPipeline);
    /// Destruct.
    ~RenderBuffer() override;
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

/// Render buffer texture creation parameters.
struct TextureRenderBufferParams
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

/// Writable and readable render buffer texture (2D or cubemap).
class URHO3D_API TextureRenderBuffer : public RenderBuffer
{
    URHO3D_OBJECT(TextureRenderBuffer, RenderBuffer);

public:
    /// Construct.
    TextureRenderBuffer(RenderPipelineInterface* renderPipeline, const TextureRenderBufferParams& params,
        const Vector2& sizeMultiplier, const IntVector2& fixedSize, bool persistent);
    /// Destruct.
    ~TextureRenderBuffer() override;

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
    TextureRenderBufferParams params_;
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

/// Write-only viewport color render buffer.
class URHO3D_API ViewportColorRenderBuffer : public RenderBuffer
{
    URHO3D_OBJECT(ViewportColorRenderBuffer, RenderBuffer);

public:
    /// Construct.
    ViewportColorRenderBuffer(RenderPipelineInterface* renderPipeline);

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

/// Write-only viewport depth-stenil texture.
class URHO3D_API ViewportDepthStencilRenderBuffer : public RenderBuffer
{
    URHO3D_OBJECT(ViewportDepthStencilRenderBuffer, RenderBuffer);

public:
    /// Construct.
    ViewportDepthStencilRenderBuffer(RenderPipelineInterface* renderPipeline);

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
