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
#include "../RenderPipeline/RenderPipelineDebugger.h"
#include "../RenderPipeline/RenderPipelineDefs.h"
#include "../RenderPipeline/ShaderConsts.h"

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
    // Assume that backbuffer always has stencil, otherwise we cannot do anything about it.
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
    IntRect viewportRect_{};
    RenderSurface* depthStencil_{};
    RenderSurface* renderTargets_[MAX_RENDERTARGETS]{};
};

ea::optional<RenderSurfaceArray> PrepareRenderSurfaces(bool ignoreRect,
    RenderBuffer* depthStencilBuffer, ea::span<RenderBuffer* const> colorBuffers, CubeMapFace face)
{
    if (!depthStencilBuffer && colorBuffers.empty())
    {
        URHO3D_LOGERROR("Depth-stencil and color buffers are null");
        return ea::nullopt;
    }

    auto nullColorBuffer = ea::find(colorBuffers.begin(), colorBuffers.end(), nullptr);
    if (nullColorBuffer != colorBuffers.end())
    {
        URHO3D_LOGERROR("Color buffer {} is null", nullColorBuffer - colorBuffers.begin());
        return ea::nullopt;
    }

    if (colorBuffers.size() > MAX_RENDERTARGETS)
    {
        URHO3D_LOGERROR("Cannot set more than {} color render buffers", MAX_RENDERTARGETS);
        return ea::nullopt;
    }

    RenderBuffer* referenceBuffer = depthStencilBuffer ? depthStencilBuffer : colorBuffers[0];
    for (unsigned i = 0; i < colorBuffers.size(); ++i)
    {
        if (!AreRenderBuffersCompatible(referenceBuffer, colorBuffers[i], ignoreRect))
        {
            URHO3D_LOGERROR("Color render buffer #{} is incompatible with other buffers", i);
            return ea::nullopt;
        }
    }

    RenderSurfaceArray result;

    if (depthStencilBuffer)
    {
        result.depthStencil_ = depthStencilBuffer->GetRenderSurface(face);
        result.viewportRect_ = depthStencilBuffer->GetViewportRect();
    }
    else
    {
        // Use temporary dirty depth buffer
        RenderBuffer* colorBuffer = colorBuffers[0];
        auto renderer = colorBuffer->GetSubsystem<Renderer>();
        result.depthStencil_ = renderer->GetDepthStencil(colorBuffer->GetRenderSurface(face));
        result.viewportRect_ = colorBuffer->GetViewportRect();
    }

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

}

RenderBufferManager::RenderBufferManager(RenderPipelineInterface* renderPipeline)
    : Object(renderPipeline->GetContext())
    , renderPipeline_(renderPipeline)
    , graphics_(GetSubsystem<Graphics>())
    , renderer_(GetSubsystem<Renderer>())
    , debugger_(renderPipeline_->GetDebugger())
    , drawQueue_(renderer_->GetDefaultDrawQueue())
{
    // Order is important. RenderBufferManager should receive callbacks before any of render buffers
    renderPipeline_->OnPipelineStatesInvalidated.Subscribe(this, &RenderBufferManager::OnPipelineStatesInvalidated);
    renderPipeline_->OnRenderBegin.Subscribe(this, &RenderBufferManager::OnRenderBegin);
    renderPipeline_->OnRenderEnd.Subscribe(this, &RenderBufferManager::OnRenderEnd);

    viewportColorBuffer_ = MakeShared<ViewportColorRenderBuffer>(renderPipeline_);
    viewportDepthBuffer_ = MakeShared<ViewportDepthStencilRenderBuffer>(renderPipeline_);
}

void RenderBufferManager::SetSettings(const RenderBufferManagerSettings& settings)
{
    settings_ = settings;
}

void RenderBufferManager::SetFrameSettings(const RenderBufferManagerFrameSettings& frameSettings)
{
    frameSettings_ = frameSettings;
    if (frameSettings_.supportColorReadWrite_)
        frameSettings_.readableColor_ = true;
}

SharedPtr<RenderBuffer> RenderBufferManager::CreateColorBuffer(const RenderBufferParams& params, const Vector2& size)
{
    return MakeShared<TextureRenderBuffer>(renderPipeline_, params, size);
}

void RenderBufferManager::SwapColorBuffers(bool synchronizeContents)
{
    if (!frameSettings_.supportColorReadWrite_)
    {
        URHO3D_LOGERROR("Cannot call SwapColorBuffers if 'supportColorReadWrite' flag is not set");
        assert(0);
        return;
    }

    assert(readableColorBuffer_ && writeableColorBuffer_);
    ea::swap(writeableColorBuffer_, readableColorBuffer_);

    if (synchronizeContents)
    {
        SetRenderTargets(depthStencilBuffer_, { writeableColorBuffer_ });
        DrawTexture("Synchronize readable and writeable color buffers", readableColorBuffer_->GetTexture2D());
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
            graphics_->SetViewport(renderSurfaces->viewportRect_);
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
    renderSurfaces.depthStencil_ = renderer_->GetDepthStencil(renderSurfaces.renderTargets_[0]);
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

Vector4 RenderBufferManager::GetDefaultClipToUVSpaceOffsetAndScale() const
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
    defines += " URHO3D_GEOMETRY_STATIC";
    if (graphics_->GetCaps().constantBuffersSupported_)
        defines += " URHO3D_USE_CBUFFERS";

    PipelineStateDesc desc;
    desc.blendMode_ = blendMode;
    desc.vertexShader_ = graphics_->GetShader(VS, shaderName, defines);
    desc.pixelShader_ = graphics_->GetShader(PS, shaderName, defines);

    return CreateQuadPipelineState(desc);
}

void RenderBufferManager::DrawQuad(ea::string_view debugComment, const DrawQuadParams& params, bool flipVertical)
{
    if (!params.pipelineState_->IsValid())
        return;

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

    if (drawQueue_->BeginShaderParameterGroup(SP_FRAME))
    {
        drawQueue_->AddShaderParameter(ShaderConsts::Frame_DeltaTime, timeStep_);
        drawQueue_->CommitShaderParameterGroup(SP_FRAME);
    }

    if (drawQueue_->BeginShaderParameterGroup(SP_CAMERA))
    {
        drawQueue_->AddShaderParameter(ShaderConsts::Camera_GBufferOffsets, params.clipToUVOffsetAndScale_);
        drawQueue_->AddShaderParameter(ShaderConsts::Camera_GBufferInvSize, params.invInputSize_);
        drawQueue_->AddShaderParameter(ShaderConsts::Camera_ViewProj, projection);
        drawQueue_->CommitShaderParameterGroup(SP_CAMERA);
    }

    if (drawQueue_->BeginShaderParameterGroup(SP_OBJECT))
    {
        drawQueue_->AddShaderParameter(ShaderConsts::Object_Model, modelMatrix);
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

    if (RenderPipelineDebugger::IsSnapshotInProgress(debugger_))
    {
        debugger_->ReportQuad(debugComment, graphics_->GetViewport().Size());
    }
}

void RenderBufferManager::DrawViewportQuad(ea::string_view debugComment,
    PipelineState* pipelineState, ea::span<const ShaderResourceDesc> resources,
    ea::span<const ShaderParameterDesc> parameters, bool flipVertical)
{
    DrawQuadParams params;
    params.pipelineState_ = pipelineState;
    params.clipToUVOffsetAndScale_ = GetDefaultClipToUVSpaceOffsetAndScale();
    params.invInputSize_ = GetInvOutputSize();
    params.resources_ = resources;
    params.parameters_ = parameters;
    DrawQuad(debugComment, params, flipVertical);
}

void RenderBufferManager::DrawFeedbackViewportQuad(ea::string_view debugComment,
    PipelineState* pipelineState, ea::span<const ShaderResourceDesc> resources,
    ea::span<const ShaderParameterDesc> parameters, bool flipVertical)
{
    DrawQuadParams params;
    params.pipelineState_ = pipelineState;
    params.clipToUVOffsetAndScale_ = GetDefaultClipToUVSpaceOffsetAndScale();
    params.invInputSize_ = GetInvOutputSize();
    params.bindSecondaryColorToDiffuse_ = true;
    params.resources_ = resources;
    params.parameters_ = parameters;
    DrawQuad(debugComment, params, flipVertical);
}

void RenderBufferManager::OnPipelineStatesInvalidated()
{
    copyTexturePipelineState_ = nullptr;
}

void RenderBufferManager::OnRenderBegin(const CommonFrameInfo& frameInfo)
{
    timeStep_ = frameInfo.timeStep_;
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

    // Determine output format
    const bool needHDR = settings_.colorSpace_ == RenderPipelineColorSpace::LinearHDR;
    const bool needSRGB = settings_.colorSpace_ == RenderPipelineColorSpace::LinearLDR;

    RenderBufferParams viewportParams;
    viewportParams.multiSampleLevel_ = settings_.inheritMultiSampleLevel_
        ? outputMultiSample : settings_.multiSampleLevel_;
    viewportParams.textureFormat_ = needHDR ? Graphics::GetRGBAFloat16Format() : Graphics::GetRGBFormat();
    viewportParams.flags_.Set(RenderBufferFlag::sRGB, needSRGB);
    viewportParams.flags_.Set(RenderBufferFlag::BilinearFiltering, settings_.filteredColor_ || isBilinearFilteredOutput);

    if (previousViewportParams_ != viewportParams)
    {
        previousViewportParams_ = viewportParams;
        ResetCachedRenderBuffers();
    }

    // Check if need to allocate secondary color buffer, substitute primary color buffer or substitute depth buffer
    const bool needSimpleTexture = frameSettings_.readableColor_ || settings_.readableDepth_
        || settings_.colorUsableWithMultipleRenderTargets_;

    const bool isColorFormatMatching = IsColorFormatMatching(outputFormat, viewportParams.textureFormat_);
    const bool isColorSRGBMatching = isOutputSRGB == viewportParams.flags_.Test(RenderBufferFlag::sRGB);
    const bool isMultiSampleMatching = outputMultiSample == viewportParams.multiSampleLevel_;
    const bool isFilterMatching = isBilinearFilteredOutput == viewportParams.flags_.Test(RenderBufferFlag::BilinearFiltering);
    const bool isColorUsageMatching = isSimpleTextureOutput || !needSimpleTexture;

    const bool needSecondaryBuffer = frameSettings_.supportColorReadWrite_;
    const bool needSubstitutePrimaryBuffer = !isColorFormatMatching
        || !isColorSRGBMatching || !isMultiSampleMatching || !isFilterMatching || !isColorUsageMatching;
    const bool needSubstituteDepthBuffer = !isMultiSampleMatching || !outputDepthStencil.has_value()
        || ((needSecondaryBuffer || needSubstitutePrimaryBuffer) && outputDepthStencil.value() == nullptr)
        || (settings_.readableDepth_ && (!outputHasReadableDepth || !isSimpleTextureOutput))
        || (settings_.stencilBuffer_ && !outputHasStencil);

    // Allocate substitute buffers if necessary
    if (needSubstitutePrimaryBuffer && !substituteRenderBuffers_[0])
    {
        substituteRenderBuffers_[0] = MakeShared<TextureRenderBuffer>(renderPipeline_, viewportParams);
    }
    if (needSecondaryBuffer && !substituteRenderBuffers_[1])
    {
        substituteRenderBuffers_[1] = MakeShared<TextureRenderBuffer>(renderPipeline_, viewportParams);
    }
    if (needSubstituteDepthBuffer && !substituteDepthBuffer_)
    {
        RenderBufferParams depthBufferParams = viewportParams;
        depthBufferParams.flags_ |= RenderBufferFlag::Persistent;
        if (settings_.readableDepth_)
        {
            depthBufferParams.textureFormat_ = settings_.stencilBuffer_
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

    if (flipColorBuffersNextTime_ && readableColorBuffer_)
        ea::swap(writeableColorBuffer_, readableColorBuffer_);
}

void RenderBufferManager::OnRenderEnd(const CommonFrameInfo& frameInfo)
{
    if (writeableColorBuffer_ != viewportColorBuffer_.Get())
    {
        Texture* colorTexture = writeableColorBuffer_->GetTexture();
        CopyTextureRegion("Copy final color to output RenderSurface", colorTexture,
            colorTexture->GetRect(), viewportColorBuffer_->GetRenderSurface(),
            viewportColorBuffer_->GetViewportRect(), ColorSpaceTransition::Automatic, false);

        // If viewport is reused for ping-ponging, optimize away final copy
        flipColorBuffersNextTime_ ^= viewportColorBuffer_ == readableColorBuffer_;
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
    copyGammaToLinearTexturePipelineState_ = CreateQuadPipelineState(BLEND_REPLACE, shaderName, "URHO3D_GAMMA_TO_LINEAR");
    copyLinearToGammaTexturePipelineState_ = CreateQuadPipelineState(BLEND_REPLACE, shaderName, "URHO3D_LINEAR_TO_GAMMA");
}

void RenderBufferManager::CopyTextureRegion(ea::string_view debugComment,
    Texture* sourceTexture, const IntRect& sourceRect,
    RenderSurface* destinationSurface, const IntRect& destinationRect, ColorSpaceTransition mode, bool flipVertical)
{
    graphics_->SetRenderTarget(0, destinationSurface);
    for (unsigned i = 1; i < MAX_RENDERTARGETS; ++i)
        graphics_->ResetRenderTarget(i);
    graphics_->SetDepthStencil(renderer_->GetDepthStencil(destinationSurface));
    graphics_->SetViewport(destinationRect);
    DrawTextureRegion(debugComment, sourceTexture, sourceRect, mode, flipVertical);
}

void RenderBufferManager::DrawTextureRegion(ea::string_view debugComment, Texture* sourceTexture,
    const IntRect& sourceRect, ColorSpaceTransition mode, bool flipVertical)
{
    if (!sourceTexture->IsInstanceOf<Texture2D>())
    {
        URHO3D_LOGERROR("Draw texture is supported only for Texture2D");
        return;
    }

    if (!copyTexturePipelineState_ || !copyGammaToLinearTexturePipelineState_ || !copyLinearToGammaTexturePipelineState_)
        InitializeCopyTexturePipelineState();

    const bool isSRGBSource = sourceTexture->GetSRGB();
    const bool isSRGBDestination = RenderSurface::GetSRGB(graphics_, graphics_->GetRenderTarget(0));

    DrawQuadParams callParams;
    if (mode == ColorSpaceTransition::None || isSRGBSource == isSRGBDestination)
        callParams.pipelineState_ = copyTexturePipelineState_;
    else if (isSRGBDestination)
        callParams.pipelineState_ = copyGammaToLinearTexturePipelineState_;
    else
        callParams.pipelineState_ = copyLinearToGammaTexturePipelineState_;

    callParams.invInputSize_ = Vector2::ONE / static_cast<Vector2>(sourceTexture->GetSize());

    const IntRect effectiveSourceRect = sourceRect != IntRect::ZERO ? sourceRect : sourceTexture->GetRect();
    callParams.clipToUVOffsetAndScale_ = CalculateViewportOffsetAndScale(sourceTexture->GetSize(), effectiveSourceRect);

    const ShaderResourceDesc shaderResources[] = { { TU_DIFFUSE, sourceTexture } };
    callParams.resources_ = shaderResources;

    DrawQuad(debugComment, callParams, flipVertical);
}

void RenderBufferManager::DrawTexture(ea::string_view debugComment, Texture* sourceTexture,
    ColorSpaceTransition mode, bool flipVertical)
{
    DrawTextureRegion(debugComment, sourceTexture, IntRect::ZERO, mode, flipVertical);
}

}
