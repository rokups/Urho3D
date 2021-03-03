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
#include "../Graphics/RenderSurface.h"
#include "../Graphics/Texture2D.h"
#include "../RenderPipeline/RenderBufferManager.h"
#include "../RenderPipeline/RenderPipelineDefs.h"

#include <EASTL/optional.h>

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

template <class T>
ea::span<const T> ToSpan(std::initializer_list<T> list)
{
    return { list.begin(), static_cast<eastl_size_t>(list.size()) };
}

Texture2D* GetParentTexture2D(RenderSurface* renderSurface)
{
    return renderSurface && renderSurface->GetParentTexture() ? renderSurface->GetParentTexture()->Cast<Texture2D>() : nullptr;
}

Texture2D* GetParentTexture2D(RenderBuffer* renderBuffer)
{
    return renderBuffer && renderBuffer->GetTexture() ? renderBuffer->GetTexture()->Cast<Texture2D>() : nullptr;
}

ea::optional<RenderSurface*> GetLinkedDepthStencil(RenderSurface* renderSurface)
{
    if (!renderSurface)
        return nullptr;
    if (RenderSurface* depthStencil = renderSurface->GetLinkedDepthStencil())
        return depthStencil;
    return ea::nullopt;
}

bool HasStencilBuffer(RenderSurface* renderSurface)
{
    // TODO(renderer): Assume that backbuffer always has stencil, otherwise we cannot do anything about it.
    if (!renderSurface)
        return true;

    const unsigned format = renderSurface->GetParentTexture()->GetFormat();
    return format == Graphics::GetDepthStencilFormat()
        || format == Graphics::GetReadableDepthStencilFormat();
}

bool HasReadableDepth(RenderSurface* renderSurface)
{
    // Backbuffer depth is never readable
    if (!renderSurface)
        return false;

    const unsigned format = renderSurface->GetParentTexture()->GetFormat();
    return format == Graphics::GetReadableDepthFormat()
        || format == Graphics::GetReadableDepthStencilFormat();
}

bool IsColorFormatMatching(unsigned outputFormat, unsigned requestedFormat)
{
    if (outputFormat == Graphics::GetRGBAFormat())
    {
        return requestedFormat == Graphics::GetRGBAFormat()
            || requestedFormat == Graphics::GetRGBFormat();
    }
    return outputFormat == requestedFormat;
}

bool AreRenderBuffersCompatible(RenderBuffer* firstBuffer, RenderBuffer* secondBuffer, bool ignoreRect)
{
    Graphics* graphics = firstBuffer->GetSubsystem<Graphics>();
    RenderSurface* firstSurface = firstBuffer->GetRenderSurface();
    RenderSurface* secondSurface = secondBuffer->GetRenderSurface();

#ifdef URHO3D_OPENGL
    // Due to FBO limitations, in OpenGL default color and depth surfaces cannot be mixed with custom ones
    if (!!firstSurface != !!secondSurface)
        return false;
#endif

    if (RenderSurface::GetMultiSample(graphics, firstSurface) != RenderSurface::GetMultiSample(graphics, secondSurface))
        return false;

    if (!ignoreRect && firstBuffer->GetViewportRect() != secondBuffer->GetViewportRect())
        return false;

    return true;
}

struct RenderSurfaceArray
{
    RenderSurface* depthStencil_{};
    RenderSurface* renderTargets_[MAX_RENDERTARGETS]{};
};

ea::optional<RenderSurfaceArray> PrepareRenderSurfaces(bool ignoreRect,
    RenderBuffer* depthStencilBuffer, ea::span<RenderBuffer* const> colorBuffers, CubeMapFace face)
{
    if (!depthStencilBuffer)
    {
        URHO3D_LOGERROR("Depth-stencil buffer is missing", MAX_RENDERTARGETS);
        return ea::nullopt;
    }

    if (colorBuffers.size() > MAX_RENDERTARGETS)
    {
        URHO3D_LOGERROR("Cannot set more than {} color render buffers", MAX_RENDERTARGETS);
        return ea::nullopt;
    }

    for (unsigned i = 0; i < colorBuffers.size(); ++i)
    {
        if (!AreRenderBuffersCompatible(depthStencilBuffer, colorBuffers[i], ignoreRect))
        {
            URHO3D_LOGERROR("Depth-stencil is incompatible with color render buffer #{}", i);
            return ea::nullopt;
        }
    }

    RenderSurfaceArray result;
    result.depthStencil_ = depthStencilBuffer->GetRenderSurface(face);
    for (unsigned i = 0; i < colorBuffers.size(); ++i)
    {
        result.renderTargets_[i] = colorBuffers[i]->GetRenderSurface(face);
        if (!result.renderTargets_[i] && i != 0)
        {
            URHO3D_LOGERROR("Default color texture can be bound only to slot #0");
            return ea::nullopt;
        }
    }

    return result;
}

void SetRenderSurfaces(Graphics* graphics, const RenderSurfaceArray& renderSurfaces)
{
    for (unsigned i = 0; i < MAX_RENDERTARGETS; ++i)
        graphics->SetRenderTarget(i, renderSurfaces.renderTargets_[i]);
    graphics->SetDepthStencil(renderSurfaces.depthStencil_);
}

Vector4 CalculateViewportOffsetAndScale(const IntVector2& textureSize, const IntRect& viewportRect)
{
    const Vector2 halfViewportScale = 0.5f * static_cast<Vector2>(viewportRect.Size()) / static_cast<Vector2>(textureSize);
    const float xOffset = static_cast<float>(viewportRect.left_) / textureSize.x_ + halfViewportScale.x_;
    const float yOffset = static_cast<float>(viewportRect.top_) / textureSize.y_ + halfViewportScale.y_;
#ifdef URHO3D_OPENGL
    return { xOffset, 1.0f - yOffset, halfViewportScale.x_, halfViewportScale.y_ };
#else
    return { xOffset, yOffset, halfViewportScale.x_, halfViewportScale.y_ };
#endif
}

RenderSurface* GetOrCreateDepthStencil(Renderer* renderer, RenderSurface* renderSurface)
{
    // If using the backbuffer, return the backbuffer depth-stencil
    if (!renderSurface)
        return nullptr;

    if (RenderSurface* linkedDepthStencil = renderSurface->GetLinkedDepthStencil())
        return linkedDepthStencil;

    return renderer->GetDepthStencil(renderSurface->GetWidth(), renderSurface->GetHeight(),
        renderSurface->GetMultiSample(), renderSurface->GetAutoResolve());
}

}

RenderBufferManager::RenderBufferManager(RenderPipelineInterface* renderPipeline)
    : Object(renderPipeline->GetContext())
    , renderPipeline_(renderPipeline)
    , graphics_(GetSubsystem<Graphics>())
    , renderer_(GetSubsystem<Renderer>())
    , drawQueue_(renderPipeline_->GetDefaultDrawQueue())
{
    // Order is important. RenderBufferManager should receive callbacks before any of render buffers
    renderPipeline_->OnPipelineStatesInvalidated.Subscribe(this, &RenderBufferManager::OnPipelineStatesInvalidated);
    renderPipeline_->OnRenderBegin.Subscribe(this, &RenderBufferManager::OnRenderBegin);
    renderPipeline_->OnRenderEnd.Subscribe(this, &RenderBufferManager::OnRenderEnd);

    viewportColorBuffer_ = MakeShared<ViewportColorRenderBuffer>(renderPipeline_);
    viewportDepthBuffer_ = MakeShared<ViewportDepthStencilRenderBuffer>(renderPipeline_);
}

void RenderBufferManager::RequestViewport(ViewportRenderBufferFlags flags, const RenderBufferParams& params)
{
    viewportFlags_ = flags;
    viewportParams_ = params;
}

SharedPtr<RenderBuffer> RenderBufferManager::CreateColorBuffer(const RenderBufferParams& params, const Vector2& size)
{
    return MakeShared<TextureRenderBuffer>(renderPipeline_, params, size);
}

void RenderBufferManager::PrepareForColorReadWrite(bool synchronizeInputAndOutput)
{
    if (!viewportFlags_.Test(ViewportRenderBufferFlag::SupportOutputColorReadWrite))
    {
        URHO3D_LOGERROR("Cannot call PrepareForColorReadWrite if SupportOutputColorReadWrite flag is not set");
        assert(0);
        return;
    }

    assert(readableColorBuffer_ && writeableColorBuffer_);
    ea::swap(writeableColorBuffer_, readableColorBuffer_);

    if (synchronizeInputAndOutput)
    {
        // TODO(renderer): Implement me
        assert(0);
    }
}

void RenderBufferManager::SetRenderTargetsRect(const IntRect& viewportRect,
    RenderBuffer* depthStencilBuffer, ea::span<RenderBuffer* const> colorBuffers, CubeMapFace face)
{
    const bool ignoreRect = viewportRect != IntRect::ZERO;
    const ea::optional<RenderSurfaceArray> renderSurfaces = PrepareRenderSurfaces(
        ignoreRect, depthStencilBuffer, colorBuffers, face);

    if (renderSurfaces)
    {
        SetRenderSurfaces(graphics_, *renderSurfaces);

        if (viewportRect == IntRect::ZERO)
            graphics_->SetViewport(depthStencilBuffer->GetViewportRect());
        else
            graphics_->SetViewport(viewportRect);
    }
}

void RenderBufferManager::SetRenderTargets(RenderBuffer* depthStencilBuffer,
    ea::span<RenderBuffer* const> colorBuffers, CubeMapFace face)
{
    SetRenderTargetsRect(IntRect::ZERO, depthStencilBuffer, colorBuffers, face);
}

void RenderBufferManager::SetRenderTargetsRect(const IntRect& viewportRect, RenderBuffer* depthStencilBuffer,
    std::initializer_list<RenderBuffer*> colorBuffers, CubeMapFace face)
{
    SetRenderTargetsRect(viewportRect, depthStencilBuffer, ToSpan(colorBuffers), face);
}

void RenderBufferManager::SetRenderTargets(RenderBuffer* depthStencilBuffer,
    std::initializer_list<RenderBuffer*> colorBuffers, CubeMapFace face)
{
    SetRenderTargetsRect(IntRect::ZERO, depthStencilBuffer, ToSpan(colorBuffers), face);
}

void RenderBufferManager::SetOutputRenderTargersRect(const IntRect& viewportRect)
{
    SetRenderTargetsRect(viewportRect, GetDepthStencilOutput(), { GetColorOutput() });
}

void RenderBufferManager::SetOutputRenderTargers()
{
    SetRenderTargetsRect(IntRect::ZERO, GetDepthStencilOutput(), { GetColorOutput() });
}

void RenderBufferManager::ClearDepthStencilRect(const IntRect& viewportRect, RenderBuffer* depthStencilBuffer,
    ClearTargetFlags flags, float depth, unsigned stencil, CubeMapFace face)
{
    RenderSurfaceArray renderSurfaces;
    renderSurfaces.depthStencil_ = depthStencilBuffer->GetRenderSurface(face);
    SetRenderSurfaces(graphics_, renderSurfaces);

    if (viewportRect == IntRect::ZERO)
        graphics_->SetViewport(depthStencilBuffer->GetViewportRect());
    else
        graphics_->SetViewport(viewportRect);
    graphics_->Clear(CLEAR_DEPTH | CLEAR_STENCIL, Color::TRANSPARENT_BLACK, depth, stencil);
}

void RenderBufferManager::ClearColorRect(const IntRect& viewportRect, RenderBuffer* colorBuffer,
    const Color& color, CubeMapFace face)
{
    RenderSurfaceArray renderSurfaces;
    renderSurfaces.renderTargets_[0] = colorBuffer->GetRenderSurface(face);
    renderSurfaces.depthStencil_ = GetOrCreateDepthStencil(renderer_, renderSurfaces.renderTargets_[0]);
    SetRenderSurfaces(graphics_, renderSurfaces);

    if (viewportRect == IntRect::ZERO)
        graphics_->SetViewport(colorBuffer->GetViewportRect());
    else
        graphics_->SetViewport(viewportRect);
    graphics_->Clear(CLEAR_COLOR, color);
}

void RenderBufferManager::ClearDepthStencil(RenderBuffer* depthStencilBuffer,
    ClearTargetFlags flags, float depth, unsigned stencil, CubeMapFace face)
{
    ClearDepthStencilRect(IntRect::ZERO, depthStencilBuffer, flags, depth, stencil, face);
}

void RenderBufferManager::ClearColor(RenderBuffer* colorBuffer,
    const Color& color, CubeMapFace face)
{
    ClearColorRect(IntRect::ZERO, colorBuffer, color, face);
}

void RenderBufferManager::ClearOutputRect(const IntRect& viewportRect, ClearTargetFlags flags,
    const Color& color, float depth, unsigned stencil)
{
    SetRenderTargetsRect(viewportRect, GetDepthStencilOutput(), { GetColorOutput() });
    graphics_->Clear(flags, color, depth, stencil);
}

void RenderBufferManager::ClearOutput(ClearTargetFlags flags, const Color& color, float depth, unsigned stencil)
{
    ClearOutputRect(IntRect::ZERO, flags, color, depth, stencil);
}

void RenderBufferManager::ClearOutput(const Color& color, float depth, unsigned stencil)
{
    ClearOutput(CLEAR_COLOR | CLEAR_DEPTH | CLEAR_STENCIL, color, depth, stencil);
}

Vector4 RenderBufferManager::GetOutputClipToUVSpaceOffsetAndScale() const
{
    const IntVector2 size = GetOutputSize();
    return CalculateViewportOffsetAndScale(size, IntRect{ IntVector2::ZERO, size });
}

SharedPtr<PipelineState> RenderBufferManager::CreateQuadPipelineState(PipelineStateDesc desc)
{
    Geometry* quadGeometry = renderer_->GetQuadGeometry();

    desc.InitializeInputLayoutAndPrimitiveType(quadGeometry);
    desc.colorWriteEnabled_ = true;

    return renderer_->GetOrCreatePipelineState(desc);
}

SharedPtr<PipelineState> RenderBufferManager::CreateQuadPipelineState(BlendMode blendMode,
    const ea::string& shaderName, const ea::string& shaderDefines)
{
    ea::string defines = shaderDefines;
    if (graphics_->GetConstantBuffersEnabled())
        defines += " URHO3D_USE_CBUFFERS";

    PipelineStateDesc desc;
    desc.blendMode_ = blendMode;
    desc.vertexShader_ = graphics_->GetShader(VS, shaderName, defines);
    desc.pixelShader_ = graphics_->GetShader(PS, shaderName, defines);

    return CreateQuadPipelineState(desc);
}

void RenderBufferManager::DrawQuad(const DrawQuadParams& params, bool flipVertical)
{
    Geometry* quadGeometry = renderer_->GetQuadGeometry();
    Matrix3x4 modelMatrix = Matrix3x4::IDENTITY;
    Matrix4 projection = Matrix4::IDENTITY;
    if (flipVertical)
        projection.m11_ = -1.0f;
#ifdef URHO3D_OPENGL
    modelMatrix.m23_ = 0.0f;
#else
    modelMatrix.m23_ = 0.5f;
#endif

    drawQueue_->Reset();
    drawQueue_->SetPipelineState(params.pipelineState_);

    if (drawQueue_->BeginShaderParameterGroup(SP_CAMERA))
    {
        drawQueue_->AddShaderParameter(VSP_GBUFFEROFFSETS, params.clipToUVOffsetAndScale_);
        drawQueue_->AddShaderParameter(PSP_GBUFFERINVSIZE, params.invInputSize_);
        drawQueue_->AddShaderParameter(VSP_VIEWPROJ, projection);
        drawQueue_->CommitShaderParameterGroup(SP_CAMERA);
    }

    if (drawQueue_->BeginShaderParameterGroup(SP_OBJECT))
    {
        drawQueue_->AddShaderParameter(VSP_MODEL, modelMatrix);
        drawQueue_->CommitShaderParameterGroup(SP_OBJECT);
    }

    if (drawQueue_->BeginShaderParameterGroup(SP_CUSTOM))
    {
        for (const ShaderParameterDesc& shaderParameter : params.parameters_)
            drawQueue_->AddShaderParameter(shaderParameter.name_, shaderParameter.value_);
        drawQueue_->CommitShaderParameterGroup(SP_CUSTOM);
    }

    if (params.bindSecondaryColorToDiffuse_)
    {
        if (Texture2D* secondaryColor = GetSecondaryColorTexture())
            drawQueue_->AddShaderResource(TU_DIFFUSE, secondaryColor);
    }
    for (const ShaderResourceDesc& shaderResource : params.resources_)
    {
        if (!params.bindSecondaryColorToDiffuse_ || shaderResource.unit_ != TU_DIFFUSE)
            drawQueue_->AddShaderResource(shaderResource.unit_, shaderResource.texture_);
    }
    drawQueue_->CommitShaderResources();

    drawQueue_->SetBuffers(GeometryBufferArray{ quadGeometry });
    drawQueue_->DrawIndexed(quadGeometry->GetIndexStart(), quadGeometry->GetIndexCount());

    drawQueue_->Execute();
}

void RenderBufferManager::DrawViewportQuad(PipelineState* pipelineState, ea::span<const ShaderResourceDesc> resources,
    ea::span<const ShaderParameterDesc> parameters, bool flipVertical)
{
    DrawQuadParams params;
    params.pipelineState_ = pipelineState;
    params.clipToUVOffsetAndScale_ = GetOutputClipToUVSpaceOffsetAndScale();
    params.invInputSize_ = GetInvOutputSize();
    params.resources_ = resources;
    params.parameters_ = parameters;
    DrawQuad(params, flipVertical);
}

void RenderBufferManager::DrawFeedbackViewportQuad(PipelineState* pipelineState, ea::span<const ShaderResourceDesc> resources,
    ea::span<const ShaderParameterDesc> parameters, bool flipVertical)
{
    DrawQuadParams params;
    params.pipelineState_ = pipelineState;
    params.clipToUVOffsetAndScale_ = GetOutputClipToUVSpaceOffsetAndScale();
    params.invInputSize_ = GetInvOutputSize();
    params.bindSecondaryColorToDiffuse_ = true;
    params.resources_ = resources;
    params.parameters_ = parameters;
    DrawQuad(params, flipVertical);
}

void RenderBufferManager::OnPipelineStatesInvalidated()
{
    copyTexturePipelineState_ = nullptr;
}

void RenderBufferManager::OnRenderBegin(const CommonFrameInfo& frameInfo)
{
    viewportRect_ = frameInfo.viewportRect_;

    // Get parameters of output render surface
    const unsigned outputFormat = RenderSurface::GetFormat(graphics_, frameInfo.renderTarget_);
    const bool isOutputSRGB = RenderSurface::GetSRGB(graphics_, frameInfo.renderTarget_);
    const int outputMultiSample = RenderSurface::GetMultiSample(graphics_, frameInfo.renderTarget_);

    const ea::optional<RenderSurface*> outputDepthStencil = GetLinkedDepthStencil(frameInfo.renderTarget_);
    const bool outputHasStencil = outputDepthStencil && HasStencilBuffer(*outputDepthStencil);
    const bool outputHasReadableDepth = outputDepthStencil && HasReadableDepth(*outputDepthStencil);

    Texture2D* outputTexture = GetParentTexture2D(frameInfo.renderTarget_);
    const bool isFullRectOutput = frameInfo.viewportRect_ == IntRect::ZERO
        || frameInfo.viewportRect_ == RenderSurface::GetRect(graphics_, frameInfo.renderTarget_);
    const bool isSimpleTextureOutput = outputTexture != nullptr && isFullRectOutput;
    const bool isBilinearFilteredOutput = outputTexture && outputTexture->GetFilterMode() != FILTER_NEAREST;

    if (viewportFlags_.Test(ViewportRenderBufferFlag::InheritColorFormat))
        viewportParams_.textureFormat_ = outputFormat;
    if (viewportFlags_.Test(ViewportRenderBufferFlag::InheritSRGB))
        viewportParams_.flags_.Assign(RenderBufferFlag::sRGB, isOutputSRGB);
    if (viewportFlags_.Test(ViewportRenderBufferFlag::InheritBilinearFiltering))
        viewportParams_.flags_.Assign(RenderBufferFlag::BilinearFiltering, isBilinearFilteredOutput);
    if (viewportFlags_.Test(ViewportRenderBufferFlag::InheritMultiSampleLevel))
        viewportParams_.multiSampleLevel_ = outputMultiSample;

    if (previousViewportParams_ != viewportParams_)
    {
        previousViewportParams_ = viewportParams_;
        ResetCachedRenderBuffers();
    }

    // Check if need to allocate secondary color buffer, substitute primary color buffer or substitute depth buffer
    const bool needReadableColor = viewportFlags_.Test(ViewportRenderBufferFlag::IsReadableColor);
    const bool needReadableDepth = viewportFlags_.Test(ViewportRenderBufferFlag::IsReadableDepth);
    const bool needStencilBuffer = viewportFlags_.Test(ViewportRenderBufferFlag::HasStencil);
    const bool needSimultaneousReadAndWrite = viewportFlags_.Test(ViewportRenderBufferFlag::SupportOutputColorReadWrite);
    const bool needViewportMRT = viewportFlags_.Test(ViewportRenderBufferFlag::UsableWithMultipleRenderTargets);

    const bool isColorFormatMatching = IsColorFormatMatching(outputFormat, viewportParams_.textureFormat_);
    const bool isColorSRGBMatching = isOutputSRGB == viewportParams_.flags_.Test(RenderBufferFlag::sRGB);
    const bool isMultiSampleMatching = outputMultiSample == viewportParams_.multiSampleLevel_;
    const bool isFilterMatching = isBilinearFilteredOutput == viewportParams_.flags_.Test(RenderBufferFlag::BilinearFiltering);

    const bool needSecondaryBuffer = needSimultaneousReadAndWrite;
    const bool needSubstitutePrimaryBuffer = !isColorFormatMatching
        || !isColorSRGBMatching || !isMultiSampleMatching || !isFilterMatching
        || ((needReadableColor || needReadableDepth || needSimultaneousReadAndWrite) && !isSimpleTextureOutput)
        || (needViewportMRT && !isSimpleTextureOutput);
    const bool needSubstituteDepthBuffer = !isMultiSampleMatching || !outputDepthStencil.has_value()
        || ((needSecondaryBuffer || needSubstitutePrimaryBuffer) && outputDepthStencil.value() == nullptr)
        || (needReadableDepth && (!outputHasReadableDepth || !isSimpleTextureOutput))
        || (needStencilBuffer && !outputHasStencil);

    // Allocate substitute buffers if necessary
    if (needSubstitutePrimaryBuffer && !substituteRenderBuffers_[0])
    {
        substituteRenderBuffers_[0] = MakeShared<TextureRenderBuffer>(renderPipeline_, viewportParams_);
    }
    if (needSecondaryBuffer && !substituteRenderBuffers_[1])
    {
        substituteRenderBuffers_[1] = MakeShared<TextureRenderBuffer>(renderPipeline_, viewportParams_);
    }
    if (needSubstituteDepthBuffer && !substituteDepthBuffer_)
    {
        RenderBufferParams depthBufferParams = viewportParams_;
        depthBufferParams.flags_ |= RenderBufferFlag::Persistent;
        if (needReadableDepth)
        {
            depthBufferParams.textureFormat_ = needStencilBuffer
                ? Graphics::GetReadableDepthStencilFormat()
                : Graphics::GetReadableDepthFormat();
        }
        else
        {
            depthBufferParams.textureFormat_ = Graphics::GetDepthStencilFormat();
        }
        substituteDepthBuffer_ = MakeShared<TextureRenderBuffer>(renderPipeline_, depthBufferParams);
    }

    depthStencilBuffer_ = needSubstituteDepthBuffer ? substituteDepthBuffer_.Get() : viewportDepthBuffer_.Get();
    writeableColorBuffer_ = needSubstitutePrimaryBuffer ? substituteRenderBuffers_[0].Get() : viewportColorBuffer_.Get();
    readableColorBuffer_ = needSecondaryBuffer ? substituteRenderBuffers_[1].Get() : nullptr;
}

void RenderBufferManager::OnRenderEnd(const CommonFrameInfo& frameInfo)
{
    if (writeableColorBuffer_ != viewportColorBuffer_.Get())
    {
        Texture* colorTexture = writeableColorBuffer_->GetTexture();
        CopyTextureRegion(colorTexture, colorTexture->GetRect(),
            viewportColorBuffer_->GetRenderSurface(), viewportColorBuffer_->GetViewportRect(), false);
    }
}

void RenderBufferManager::ResetCachedRenderBuffers()
{
    substituteRenderBuffers_[0] = nullptr;
    substituteRenderBuffers_[1] = nullptr;
    substituteDepthBuffer_ = nullptr;
}

void RenderBufferManager::InitializeCopyTexturePipelineState()
{
    static const char* shaderName = "v2/CopyFramebuffer";
    copyTexturePipelineState_ = CreateQuadPipelineState(BLEND_REPLACE, shaderName, "");
}

void RenderBufferManager::CopyTextureRegion(Texture* sourceTexture, const IntRect& sourceRect,
    RenderSurface* destinationSurface, const IntRect& destinationRect, bool flipVertical)
{
    if (!sourceTexture->IsInstanceOf<Texture2D>())
    {
        URHO3D_LOGERROR("Copy region is supported only for Texture2D");
        return;
    }

    if (!copyTexturePipelineState_)
    {
        InitializeCopyTexturePipelineState();
        if (!copyTexturePipelineState_)
            return;
    }

    graphics_->SetRenderTarget(0, destinationSurface);
    for (unsigned i = 1; i < MAX_RENDERTARGETS; ++i)
        graphics_->ResetRenderTarget(i);
    graphics_->SetDepthStencil(GetOrCreateDepthStencil(renderer_, destinationSurface));
    graphics_->SetViewport(destinationRect);

    DrawQuadParams callParams;
    callParams.pipelineState_ = copyTexturePipelineState_;
    callParams.invInputSize_ = Vector2::ONE / static_cast<Vector2>(sourceTexture->GetSize());
    callParams.clipToUVOffsetAndScale_ = CalculateViewportOffsetAndScale(sourceTexture->GetSize(), sourceRect);

    const ShaderResourceDesc shaderResources[] = { { TU_DIFFUSE, sourceTexture } };
    callParams.resources_ = shaderResources;

    DrawQuad(callParams, flipVertical);
}

}
