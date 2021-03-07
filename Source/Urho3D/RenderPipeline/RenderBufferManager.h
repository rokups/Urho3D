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
#include "../Core/Object.h"
#include "../Graphics/DrawCommandQueue.h"
#include "../Graphics/GraphicsDefs.h"
#include "../Graphics/PipelineState.h"
#include "../RenderPipeline/RenderBuffer.h"

namespace Urho3D
{

class RenderPipelineInterface;
struct FrameInfo;

/// Pipeline state, shader parameters and shader resources needed to draw a fullscreen quad.
/// clipToUVOffsetAndScale_ and invInputSize_ are filled automatically for viewport quad.
struct DrawQuadParams
{
    PipelineState* pipelineState_{};
    Vector4 clipToUVOffsetAndScale_;
    Vector2 invInputSize_;
    bool bindSecondaryColorToDiffuse_{};
    ea::span<const ShaderResourceDesc> resources_;
    ea::span<const ShaderParameterDesc> parameters_;
};

/// Class that manages all render buffers within viewport and viewport itself.
class URHO3D_API RenderBufferManager : public Object
{
    URHO3D_OBJECT(RenderBufferManager, Object);

public:
    explicit RenderBufferManager(RenderPipelineInterface* renderPipeline);

    /// Request certain behaviour from output color and depth-stencil buffers.
    /// This call is lightweight, it's fine to call it on every frame.
    void RequestViewport(ViewportRenderBufferFlags flags, const RenderBufferParams& params);

    SharedPtr<RenderBuffer> CreateColorBuffer(const RenderBufferParams& params, const Vector2& size = Vector2::ONE);

    /// Prepare for simultaneous reading from and writing to color buffer.
    /// Invalid if SupportOutputColorReadWrite is not requested.
    void PrepareForColorReadWrite(bool synchronizeInputAndOutput);

    /// Set depth-stencil and color buffers. Depth-stencil is required, color buffers are optional.
    /// All render buffers should have same multisample levels.
    /// If rectangle is not specified, all render buffers should have same size and viewport rectanges.
    /// @{
    void SetRenderTargetsRect(const IntRect& viewportRect, RenderBuffer* depthStencilBuffer,
        ea::span<RenderBuffer* const> colorBuffers, CubeMapFace face = FACE_POSITIVE_X);
    void SetRenderTargets(RenderBuffer* depthStencilBuffer,
        ea::span<RenderBuffer* const> colorBuffers, CubeMapFace face = FACE_POSITIVE_X);

    void SetRenderTargetsRect(const IntRect& viewportRect, RenderBuffer* depthStencilBuffer,
        std::initializer_list<RenderBuffer*> colorBuffers, CubeMapFace face = FACE_POSITIVE_X);
    void SetRenderTargets(RenderBuffer* depthStencilBuffer,
        std::initializer_list<RenderBuffer*> colorBuffers, CubeMapFace face = FACE_POSITIVE_X);

    void SetOutputRenderTargersRect(const IntRect& viewportRect);
    void SetOutputRenderTargers();
    /// @}

    /// Clear color, depth and/or stencil render target(s).
    /// If rectangle is specified, only part of render target is cleared.
    /// Changes currently bound depth-stencil and render targets.
    /// @{
    void ClearDepthStencilRect(const IntRect& viewportRect, RenderBuffer* depthStencilBuffer,
        ClearTargetFlags flags, float depth, unsigned stencil, CubeMapFace face = FACE_POSITIVE_X);
    void ClearDepthStencil(RenderBuffer* depthStencilBuffer,
        ClearTargetFlags flags, float depth, unsigned stencil, CubeMapFace face = FACE_POSITIVE_X);

    void ClearColorRect(const IntRect& viewportRect, RenderBuffer* colorBuffer,
        const Color& color, CubeMapFace face = FACE_POSITIVE_X);
    void ClearColor(RenderBuffer* colorBuffer,
        const Color& color, CubeMapFace face = FACE_POSITIVE_X);

    /// Clears currently writable color output if more than one color buffers are used.
    void ClearOutputRect(const IntRect& viewportRect, ClearTargetFlags flags,
        const Color& color, float depth, unsigned stencil);
    void ClearOutput(ClearTargetFlags flags, const Color& color, float depth, unsigned stencil);
    void ClearOutput(const Color& color, float depth, unsigned stencil);
    /// @}

    /// Create pipeline states for fullscreen quad rendering
    /// @{
    SharedPtr<PipelineState> CreateQuadPipelineState(PipelineStateDesc desc);
    SharedPtr<PipelineState> CreateQuadPipelineState(BlendMode blendMode,
        const ea::string& shaderName, const ea::string& shaderDefines);
    /// @}

    /// Render fullscreen quad with custom parameters into currently bound render buffer.
    void DrawQuad(const DrawQuadParams& params, bool flipVertical = false);
    /// Render fullscreen quad into currently bound viewport-sized render buffer.
    void DrawViewportQuad(PipelineState* pipelineState, ea::span<const ShaderResourceDesc> resources,
        ea::span<const ShaderParameterDesc> parameters, bool flipVertical = false);
    /// Render fullscreen quad into currently bound viewport-sized render buffer.
    /// Current secondary color render buffer is passed as diffuse texture input.
    void DrawFeedbackViewportQuad(PipelineState* pipelineState, ea::span<const ShaderResourceDesc> resources,
        ea::span<const ShaderParameterDesc> parameters, bool flipVertical = false);

    /// Return depth-stencil buffer. Stays the same during the frame.
    RenderBuffer* GetDepthStencilOutput() const { return depthStencilBuffer_; }
    /// Return color render buffer. Changes on PrepareForColorReadWrite.
    RenderBuffer* GetColorOutput() const { return writeableColorBuffer_; }

    /// Return readable depth texture.
    /// It's not allowed to write depth and read it from shader input simultaneously for same depth-stencil buffer.
    Texture2D* GetDepthStencilTexture() const { return depthStencilBuffer_->GetTexture2D(); };
    /// Return secondary color render buffer texture that can be used while writing to color render buffer.
    /// Texture content is defined at the moment of previous PrepareForColorReadWrite.
    /// Content is undefined if PrepareForColorReadWrite was not called during current frame.
    Texture2D* GetSecondaryColorTexture() const { return readableColorBuffer_->GetTexture2D(); }

    /// Return size of output region (not size of output texture itself).
    IntVector2 GetOutputSize() const { return viewportRect_.Size(); }
    Vector2 GetInvOutputSize() const { return Vector2::ONE / static_cast<Vector2>(GetOutputSize()); }
    /// Return offset and scale used to convert clip space to UV space.
    /// Ignores actual viewport rectangle.
    Vector4 GetOutputClipToUVSpaceOffsetAndScale() const;

private:
    /// RenderPipeline callbacks
    /// @{
    void OnPipelineStatesInvalidated();
    void OnRenderBegin(const CommonFrameInfo& frameInfo);
    void OnRenderEnd(const CommonFrameInfo& frameInfo);
    /// @}

    void InitializeCopyTexturePipelineState();
    void ResetCachedRenderBuffers();
    void CopyTextureRegion(Texture* sourceTexture, const IntRect& sourceRect,
        RenderSurface* destinationSurface, const IntRect& destinationRect, bool flipVertical);

    /// Extrenal dependencies
    /// @{
    RenderPipelineInterface* renderPipeline_{};
    Graphics* graphics_{};
    Renderer* renderer_{};
    SharedPtr<DrawCommandQueue> drawQueue_;
    /// @}

    /// Cached between frames
    /// @{
    SharedPtr<RenderBuffer> viewportColorBuffer_;
    SharedPtr<RenderBuffer> viewportDepthBuffer_;

    SharedPtr<PipelineState> copyTexturePipelineState_;
    SharedPtr<PipelineState> copyGammaToLinearTexturePipelineState_;
    SharedPtr<PipelineState> copyLinearToGammaTexturePipelineState_;

    SharedPtr<RenderBuffer> substituteRenderBuffers_[2];
    SharedPtr<RenderBuffer> substituteDepthBuffer_;

    RenderBufferParams previousViewportParams_;
    /// @}

    /// State of current frame
    /// @{
    ViewportRenderBufferFlags viewportFlags_;
    RenderBufferParams viewportParams_;

    IntRect viewportRect_;

    RenderBuffer* depthStencilBuffer_{};
    RenderBuffer* writeableColorBuffer_{};
    RenderBuffer* readableColorBuffer_{};
    /// @}
};

}
