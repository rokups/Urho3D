#if 0
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
#include "../Graphics/Camera.h"
#include "../Graphics/DrawCommandQueue.h"
#include "../Graphics/GraphicsDefs.h"
#include "../Graphics/PipelineState.h"
#include "../Graphics/PipelineStateTracker.h"
#include "../Graphics/Viewport.h"

namespace Urho3D
{

class RenderSurface;

/// Description of render target in render pipeline.
struct RenderTargetDesc
{
    /// Render target name.
    ea::string name_;
    /// Texture format.
    unsigned format_{};
    /// Size multiplier.
    Vector2 sizeMultiplier_ = Vector2::ONE;
    /// Fixed size. If zero, ignored.
    IntVector2 fixedSize_;
    /// Whether the render target is cubemap.
    bool cubemap_{};
    /// Whether to use bilinear filtering.
    bool filtered_{};
    /// Whether is sRGB (if supported).
    bool sRGB_{};
    /// Multisample level.
    int multiSample_{ 1 };
    /// Whether to automatically resolve multisampled texture.
    bool autoResolve_{ true };
    /// Persistence key. If not 0, render target will not be reused. Must be globally unique.
    unsigned persistenceKey_{};
};

/// Viewport of render pipeline.
class RenderPipelineViewport : public Object, public PipelineStateTracker
{
    URHO3D_OBJECT(RenderPipelineViewport, Object);

public:
    /// Construct.
    explicit RenderPipelineViewport(Context* context);

    /// Initialize render target and viewport.
    void Define(RenderSurface* renderTarget, Viewport* viewport);

    /// Reset render targets.
    void ResetRenderTargets();
    /// Add generic render target.
    void AddRenderTarget(const RenderTargetDesc& renderTarget);
    /// Add simple render target.
    void AddRenderTarget(const ea::string& name, const ea::string& format);

    /// Begin frame.
    void BeginFrame();

    /// Return whether the pipeline state caches shall be invalidated.
    bool ArePipelineStatesInvalidated() const { return GetPipelineStateHash() != cachedPipelineStateHash_; }
    /// Return output viewport rectange.
    IntRect GetViewportRect() const { return viewportRect_; }
    /// Return render target by name.
    Texture* GetRenderTarget(StringHash name) const;

    /// Set render targets with default viewport and cubemap face (if applicable).
    void SetRenderTargets(ea::string_view depthStencil, ea::span<const ea::string_view> renderTargets);
    /// Set render targets.
    void SetRenderTargets(ea::string_view depthStencil, ea::span<const ea::string_view> renderTargets,
        const IntRect& viewportRect, CubeMapFace face);
    /// Clear render target.
    void ClearRenderTarget(ea::string_view renderTarget, const Color& color, CubeMapFace face = FACE_POSITIVE_X);
    /// Clear depth stencil.
    void ClearDepthStencil(ea::string_view depthStencil, float depth, unsigned stencil);

    /// Set viewport render surface and/or depth-stencil as render target and optionally clear it.
    void SetViewportRenderTargets(ClearTargetFlags clear, const Color& color, float depth, unsigned stencil);
    /// Set viewport render surface and/or depth-stencil as render target.
    void SetViewportRenderTargets();

    /// Copy texture to render target.
    void CopyToRenderTarget(Texture* sourceTexture, RenderSurface* destinationSurface,
        const IntRect& sourceViewportRect, const IntRect& destinationViewportRect, bool flipVertical);
    /// Copy one render surface to another.
    void CopyToRenderTarget(ea::string_view sourceRenderTarget, ea::string_view destinationRenderTarget,
        CubeMapFace face = FACE_POSITIVE_X);
    /// Copy render surface to viewport.
    void CopyToViewportRenderTarget(ea::string_view sourceRenderTarget);

    /// End frame.
    void EndFrame();

    /// Return GBuffer offsets.
    static Vector4 GetGBufferOffsets(const IntVector2& textureSize, const IntRect& viewportRect);
    /// Return GBuffer inverted size.
    static Vector2 GetGBufferInvSize(const IntVector2& textureSize);

protected:
    /// Mark pipeline state hash as dirty.
    unsigned RecalculatePipelineStateHash() const override;

private:
    /// Graphics subsystem.
    Graphics* graphics_{};
    /// Renderer subsystem.
    Renderer* renderer_{};

    // TODO(renderer): Fixme
public:
    /// Viewport.
    Viewport* viewport_{};
    /// Destination render target aliased as "viewport".
    RenderSurface* viewportRenderTarget_{};
private:
    /// Intermediate render targets.
    ea::vector<RenderTargetDesc> renderTargets_;

    /// Viewport rectangle.
    IntRect viewportRect_;
    /// Viewport depth texture.
    RenderSurface* viewportDepth_{};
    /// Index of render targets.
    ea::unordered_map<StringHash, Texture*> renderTargetTextures_;
    /// Pipeline state for render target copying.
    SharedPtr<PipelineState> copyRenderTargetPipelineState_;
    /// Internal draw command queue.
    DrawCommandQueue drawQueue_;

    /// Cached pipeline state hash from previous frame.
    unsigned cachedPipelineStateHash_{};
    /// Cull camera.
    Camera* cullCamera_{};
    /// Whether the constant buffers are enabled.
    bool constantBuffersEnabled_{};
};

}
#endif