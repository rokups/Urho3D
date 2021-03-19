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

#include "../RenderPipeline/RenderBufferManager.h"
#include "../RenderPipeline/RenderPipelineDefs.h"
#include "../RenderPipeline/PostProcessPass.h"

#include "../Graphics/Graphics.h"
#include "../Graphics/Texture2D.h"

#include "../DebugNew.h"

namespace Urho3D
{

PostProcessPass::PostProcessPass(RenderPipelineInterface* renderPipeline, RenderBufferManager* renderBufferManager)
    : Object(renderPipeline->GetContext())
    , renderBufferManager_(renderBufferManager)
{
}

PostProcessPass::~PostProcessPass()
{
}

SimplePostProcessPass::SimplePostProcessPass(
    RenderPipelineInterface* renderPipeline, RenderBufferManager* renderBufferManager,
    PostProcessPassFlags flags, BlendMode blendMode,
    const ea::string& shaderName, const ea::string& shaderDefines)
    : PostProcessPass(renderPipeline, renderBufferManager)
    , flags_(flags)
    , pipelineState_(renderBufferManager_->CreateQuadPipelineState(blendMode, shaderName, shaderDefines))
{
}

void SimplePostProcessPass::AddShaderParameter(StringHash name, const Variant& value)
{
    shaderParameters_.push_back(ShaderParameterDesc{ name, value });
}

void SimplePostProcessPass::AddShaderResource(TextureUnit unit, Texture* texture)
{
    shaderResources_.push_back(ShaderResourceDesc{ unit, texture });
}

void SimplePostProcessPass::Execute()
{
    if (!pipelineState_->IsValid())
        return;

    const bool colorReadAndWrite = flags_.Test(PostProcessPassFlag::NeedColorOutputReadAndWrite);

    if (colorReadAndWrite)
        renderBufferManager_->PrepareForColorReadWrite(false);
    renderBufferManager_->SetOutputRenderTargers();

    if (colorReadAndWrite)
        renderBufferManager_->DrawFeedbackViewportQuad(pipelineState_, shaderResources_, shaderParameters_);
    else
        renderBufferManager_->DrawViewportQuad(pipelineState_, shaderResources_, shaderParameters_);
}

AutoExposurePostProcessPass::AutoExposurePostProcessPass(
    RenderPipelineInterface* renderPipeline, RenderBufferManager* renderBufferManager)
    : PostProcessPass(renderPipeline, renderBufferManager)
{
    const RenderBufferFlags flagFixedBilinear = RenderBufferFlag::BilinearFiltering | RenderBufferFlag::FixedTextureSize;
    const RenderBufferFlags flagFixedNearest = RenderBufferFlag::FixedTextureSize;
    const RenderBufferFlags flagFixedNearestPersistent = RenderBufferFlag::FixedTextureSize | RenderBufferFlag::Persistent;
    const auto rgbaFormat = Graphics::GetRGBAFloat16Format();
    const auto rgFormat = Graphics::GetRGFloat16Format();

    textureHDR128_ = renderBufferManager_->CreateColorBuffer({ rgbaFormat, 1, flagFixedBilinear }, { 128, 128 });
    textureLum64_ = renderBufferManager_->CreateColorBuffer({ rgFormat, 1, flagFixedBilinear }, { 64, 64 });
    textureLum16_ = renderBufferManager_->CreateColorBuffer({ rgFormat, 1, flagFixedBilinear }, { 16, 16 });
    textureLum4_ = renderBufferManager_->CreateColorBuffer({ rgFormat, 1, flagFixedBilinear }, { 4, 4 });
    textureLum1_ = renderBufferManager_->CreateColorBuffer({ rgFormat, 1, flagFixedNearest }, { 1, 1 });
    textureAdaptedLum_ = renderBufferManager_->CreateColorBuffer({ rgFormat, 1, flagFixedNearestPersistent }, { 1, 1 });
    texturePrevAdaptedLum_ = renderBufferManager_->CreateColorBuffer({ rgFormat, 1, flagFixedNearest }, { 1, 1 });

    pipelineStateLum64_ = renderBufferManager_->CreateQuadPipelineState(BLEND_REPLACE, "v2/P_AutoExposure", "LUMINANCE64");
    pipelineStateLum16_ = renderBufferManager_->CreateQuadPipelineState(BLEND_REPLACE, "v2/P_AutoExposure", "LUMINANCE16");
    pipelineStateLum4_ = renderBufferManager_->CreateQuadPipelineState(BLEND_REPLACE, "v2/P_AutoExposure", "LUMINANCE4");
    pipelineStateLum1_ = renderBufferManager_->CreateQuadPipelineState(BLEND_REPLACE, "v2/P_AutoExposure", "LUMINANCE1");
    pipelineStateAdaptedLum_ = renderBufferManager_->CreateQuadPipelineState(BLEND_REPLACE, "v2/P_AutoExposure", "ADAPTLUMINANCE");
    pipelineStateCommitLinear_  = renderBufferManager_->CreateQuadPipelineState(BLEND_REPLACE, "v2/P_AutoExposure", "EXPOSE");
    pipelineStateCommitGamma_  = renderBufferManager_->CreateQuadPipelineState(BLEND_REPLACE, "v2/P_AutoExposure", "EXPOSE GAMMA");
}

void AutoExposurePostProcessPass::Execute()
{
    renderBufferManager_->PrepareForColorReadWrite(false);
    Texture2D* viewportTexture = renderBufferManager_->GetSecondaryColorTexture();

    renderBufferManager_->SetRenderTargets(nullptr, { textureHDR128_ });
    renderBufferManager_->DrawTexture(ColorSpaceTransition::ToLinear, viewportTexture);

    ShaderResourceDesc lumInputResources[1];
    lumInputResources[0].unit_ = TU_DIFFUSE;
    ShaderParameterDesc lumInputParameters[1];
    DrawQuadParams lumParams;
    lumParams.resources_ = lumInputResources;
    lumParams.parameters_ = lumInputParameters;
    lumParams.clipToUVOffsetAndScale_ = renderBufferManager_->GetDefaultClipToUVSpaceOffsetAndScale();

    lumInputResources[0].texture_ = textureHDR128_->GetTexture2D();
    lumInputParameters[0] = { "HDR128InvSize", Vector2::ONE / 128.0f };
    lumParams.pipelineState_ = pipelineStateLum64_;
    renderBufferManager_->SetRenderTargets(nullptr, { textureLum64_ });
    renderBufferManager_->DrawQuad(lumParams);

    lumInputResources[0].texture_ = textureLum64_->GetTexture2D();
    lumInputParameters[0] = { "Lum64InvSize", Vector2::ONE / 64.0f };
    lumParams.pipelineState_ = pipelineStateLum16_;
    renderBufferManager_->SetRenderTargets(nullptr, { textureLum16_ });
    renderBufferManager_->DrawQuad(lumParams);

    lumInputResources[0].texture_ = textureLum16_->GetTexture2D();
    lumInputParameters[0] = { "Lum16InvSize", Vector2::ONE / 16.0f };
    lumParams.pipelineState_ = pipelineStateLum4_;
    renderBufferManager_->SetRenderTargets(nullptr, { textureLum4_ });
    renderBufferManager_->DrawQuad(lumParams);

    lumInputResources[0].texture_ = textureLum4_->GetTexture2D();
    lumInputParameters[0] = { "Lum4InvSize", Vector2::ONE / 4.0f };
    lumParams.pipelineState_ = pipelineStateLum1_;
    renderBufferManager_->SetRenderTargets(nullptr, { textureLum1_ });
    renderBufferManager_->DrawQuad(lumParams);

    if (isFirstFrame_)
    {
        isFirstFrame_ = false;
        renderBufferManager_->SetRenderTargets(nullptr, { texturePrevAdaptedLum_ });
        renderBufferManager_->DrawTexture(ColorSpaceTransition::Automatic, textureLum1_->GetTexture2D());
    }
    else
    {
        renderBufferManager_->SetRenderTargets(nullptr, { texturePrevAdaptedLum_ });
        renderBufferManager_->DrawTexture(ColorSpaceTransition::Automatic, textureAdaptedLum_->GetTexture2D());
    }

    const ShaderResourceDesc adaptLumInputResources[] = {
        { TU_DIFFUSE, texturePrevAdaptedLum_->GetTexture2D() },
        { TU_NORMAL, textureLum1_->GetTexture2D() }
    };
    const ShaderParameterDesc adaptLumInputParameters[] = {
        { "AutoExposureAdaptRate", 0.6f },
        { "AutoExposureLumRange", Vector2{ 0.01f, 1.0f } },
    };
    DrawQuadParams adaptLumParams;
    adaptLumParams.resources_ = adaptLumInputResources;
    adaptLumParams.parameters_ = adaptLumInputParameters;
    adaptLumParams.pipelineState_ = pipelineStateAdaptedLum_;
    adaptLumParams.clipToUVOffsetAndScale_ = renderBufferManager_->GetDefaultClipToUVSpaceOffsetAndScale();
    renderBufferManager_->SetRenderTargets(nullptr, { textureAdaptedLum_ });
    renderBufferManager_->DrawQuad(adaptLumParams);

    const ShaderResourceDesc commitInputResources[] = { { TU_NORMAL, textureAdaptedLum_->GetTexture2D() } };
    const ShaderParameterDesc commitInputParameters[] = { { "AutoExposureMiddleGrey", 0.6f } };
    renderBufferManager_->SetOutputRenderTargers();
    PipelineState* pipelineState = viewportTexture->GetSRGB() ? pipelineStateCommitLinear_ : pipelineStateCommitGamma_;
    renderBufferManager_->DrawFeedbackViewportQuad(pipelineState, commitInputResources, commitInputParameters);
}

}
