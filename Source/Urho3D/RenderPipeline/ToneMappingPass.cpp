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
#include "../Graphics/Texture2D.h"
#include "../RenderPipeline/RenderBufferManager.h"
#include "../RenderPipeline/ToneMappingPass.h"

#include "../DebugNew.h"

namespace Urho3D
{

ToneMappingPass::ToneMappingPass(
    RenderPipelineInterface* renderPipeline, RenderBufferManager* renderBufferManager)
    : PostProcessPass(renderPipeline, renderBufferManager)
{
    InitializeTextures();
}

void ToneMappingPass::SetSettings(const ToneMappingPassSettings& settings)
{
    if (settings_ != settings)
    {
        if (settings_.mode_ != settings.mode_ || settings_.autoExposure_ != settings.autoExposure_)
            pipelineStates_ = ea::nullopt;
        settings_ = settings;
    }
}

void ToneMappingPass::InitializeTextures()
{
    const RenderBufferFlags flagFixedBilinear = RenderBufferFlag::BilinearFiltering | RenderBufferFlag::FixedTextureSize;
    const RenderBufferFlags flagFixedNearest = RenderBufferFlag::FixedTextureSize;
    const RenderBufferFlags flagFixedNearestPersistent = RenderBufferFlag::FixedTextureSize | RenderBufferFlag::Persistent;
    const auto rgbaFormat = Graphics::GetRGBAFloat16Format();
    const auto rgFormat = Graphics::GetRGFloat16Format();

    textures_ = CachedTextures{};
    textures_->color128_ = renderBufferManager_->CreateColorBuffer({ rgbaFormat, 1, flagFixedBilinear }, { 128, 128 });
    textures_->lum64_ = renderBufferManager_->CreateColorBuffer({ rgFormat, 1, flagFixedBilinear }, { 64, 64 });
    textures_->lum16_ = renderBufferManager_->CreateColorBuffer({ rgFormat, 1, flagFixedBilinear }, { 16, 16 });
    textures_->lum4_ = renderBufferManager_->CreateColorBuffer({ rgFormat, 1, flagFixedBilinear }, { 4, 4 });
    textures_->lum1_ = renderBufferManager_->CreateColorBuffer({ rgFormat, 1, flagFixedNearest }, { 1, 1 });
    textures_->adaptedLum_ = renderBufferManager_->CreateColorBuffer({ rgFormat, 1, flagFixedNearestPersistent }, { 1, 1 });
    textures_->prevAdaptedLum_ = renderBufferManager_->CreateColorBuffer({ rgFormat, 1, flagFixedNearest }, { 1, 1 });
}

void ToneMappingPass::InitializeStates()
{
    pipelineStates_ = CachedStates{};
    pipelineStates_->lum64_ = renderBufferManager_->CreateQuadPipelineState(BLEND_REPLACE, "v2/P_AutoExposure", "LUMINANCE64");
    pipelineStates_->lum16_ = renderBufferManager_->CreateQuadPipelineState(BLEND_REPLACE, "v2/P_AutoExposure", "LUMINANCE16");
    pipelineStates_->lum4_ = renderBufferManager_->CreateQuadPipelineState(BLEND_REPLACE, "v2/P_AutoExposure", "LUMINANCE4");
    pipelineStates_->lum1_ = renderBufferManager_->CreateQuadPipelineState(BLEND_REPLACE, "v2/P_AutoExposure", "LUMINANCE1");
    pipelineStates_->adaptedLum_ = renderBufferManager_->CreateQuadPipelineState(BLEND_REPLACE, "v2/P_AutoExposure", "ADAPTLUMINANCE");

    ea::string toneMappingDefines = "EXPOSE ";
    switch (settings_.mode_)
    {
    case ToneMappingMode::Reinhard:
        toneMappingDefines += "REINHARD ";
        break;
    case ToneMappingMode::ReinhardWhite:
        toneMappingDefines += "REINHARDWHITE ";
        break;
    case ToneMappingMode::Uncharted2:
        toneMappingDefines += "UNCHARTED2 ";
        break;
    default:
        break;
    }
    if (settings_.autoExposure_)
        toneMappingDefines += "AUTOEXPOSURE ";

    pipelineStates_->toneMapping_  = renderBufferManager_->CreateQuadPipelineState(
        BLEND_REPLACE, "v2/P_AutoExposure", toneMappingDefines);
}

void ToneMappingPass::EvaluateDownsampledColorBuffer()
{
    renderBufferManager_->PrepareForColorReadWrite(false);
    Texture2D* viewportTexture = renderBufferManager_->GetSecondaryColorTexture();

    renderBufferManager_->SetRenderTargets(nullptr, { textures_->color128_ });
    renderBufferManager_->DrawTexture(ColorSpaceTransition::ToLinear, viewportTexture);
}

void ToneMappingPass::EvaluateLuminance()
{
    ShaderResourceDesc shaderResources[1];
    shaderResources[0].unit_ = TU_DIFFUSE;
    ShaderParameterDesc shaderParameters[1];
    shaderParameters[0].name_ = "InputInvSize";

    DrawQuadParams drawParams;
    drawParams.resources_ = shaderResources;
    drawParams.parameters_ = shaderParameters;
    drawParams.clipToUVOffsetAndScale_ = renderBufferManager_->GetDefaultClipToUVSpaceOffsetAndScale();

    shaderResources[0].texture_ = textures_->color128_->GetTexture2D();
    shaderParameters[0].value_ = Vector2::ONE / 128.0f;
    drawParams.pipelineState_ = pipelineStates_->lum64_;
    renderBufferManager_->SetRenderTargets(nullptr, { textures_->lum64_ });
    renderBufferManager_->DrawQuad(drawParams);

    shaderResources[0].texture_ = textures_->lum64_->GetTexture2D();
    shaderParameters[0].value_ = Vector2::ONE / 64.0f;
    drawParams.pipelineState_ = pipelineStates_->lum16_;
    renderBufferManager_->SetRenderTargets(nullptr, { textures_->lum16_ });
    renderBufferManager_->DrawQuad(drawParams);

    shaderResources[0].texture_ = textures_->lum16_->GetTexture2D();
    shaderParameters[0].value_ = Vector2::ONE / 16.0f;
    drawParams.pipelineState_ = pipelineStates_->lum4_;
    renderBufferManager_->SetRenderTargets(nullptr, { textures_->lum4_ });
    renderBufferManager_->DrawQuad(drawParams);

    shaderResources[0].texture_ = textures_->lum4_->GetTexture2D();
    shaderParameters[0].value_ = Vector2::ONE / 4.0f;
    drawParams.pipelineState_ = pipelineStates_->lum1_;
    renderBufferManager_->SetRenderTargets(nullptr, { textures_->lum1_ });
    renderBufferManager_->DrawQuad(drawParams);
}

void ToneMappingPass::EvaluateAdaptedLuminance()
{
    renderBufferManager_->SetRenderTargets(nullptr, { textures_->prevAdaptedLum_ });
    RenderBuffer* sourceBuffer = isAdaptedLuminanceInitialized_ ? textures_->adaptedLum_ : textures_->lum1_;
    renderBufferManager_->DrawTexture(ColorSpaceTransition::Automatic, sourceBuffer->GetTexture2D());

    const ShaderResourceDesc shaderResources[] = {
        { TU_DIFFUSE, textures_->prevAdaptedLum_->GetTexture2D() },
        { TU_NORMAL, textures_->lum1_->GetTexture2D() }
    };
    const ShaderParameterDesc shaderParameters[] = {
        { "AdaptRate", settings_.adaptRate_ },
    };

    DrawQuadParams drawParams;
    drawParams.resources_ = shaderResources;
    drawParams.parameters_ = shaderParameters;
    drawParams.pipelineState_ = pipelineStates_->adaptedLum_;
    drawParams.clipToUVOffsetAndScale_ = renderBufferManager_->GetDefaultClipToUVSpaceOffsetAndScale();
    renderBufferManager_->SetRenderTargets(nullptr, { textures_->adaptedLum_ });
    renderBufferManager_->DrawQuad(drawParams);

    isAdaptedLuminanceInitialized_ = true;
}

void ToneMappingPass::Execute()
{
    if (!pipelineStates_)
        InitializeStates();

    EvaluateDownsampledColorBuffer();
    EvaluateLuminance();
    EvaluateAdaptedLuminance();

    const ShaderResourceDesc shaderResources[] = {
        { TU_NORMAL, textures_->adaptedLum_->GetTexture2D() }
    };
    const ShaderParameterDesc shaderParameters[] = {
        { "MinMaxExposure", Vector2(settings_.minExposure_, settings_.maxExposure_) },
        { "AutoExposureMiddleGrey", 0.6f },
    };
    renderBufferManager_->SetOutputRenderTargers();
    renderBufferManager_->DrawFeedbackViewportQuad(pipelineStates_->toneMapping_, shaderResources, shaderParameters);
}

}
