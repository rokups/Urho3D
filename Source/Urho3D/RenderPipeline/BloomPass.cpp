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
#include "../RenderPipeline/BloomPass.h"

#include "../DebugNew.h"

namespace Urho3D
{

BloomPass::BloomPass(
    RenderPipelineInterface* renderPipeline, RenderBufferManager* renderBufferManager)
    : PostProcessPass(renderPipeline, renderBufferManager)
{
    InitializeTextures();
}

void BloomPass::SetSettings(const BloomPassSettings& settings)
{
    if (settings_ != settings)
    {
        settings_ = settings;
    }
}

void BloomPass::InitializeTextures()
{
    const auto rgbFormat = Graphics::GetRGBFormat();
    const Vector2 sizeMultiplier = Vector2::ONE / 4.0f;

    textures_.blurH_ = renderBufferManager_->CreateColorBuffer(
        { rgbFormat, 1, RenderBufferFlag::BilinearFiltering }, sizeMultiplier);
    textures_.blurV_ = renderBufferManager_->CreateColorBuffer(
        { rgbFormat, 1, RenderBufferFlag::BilinearFiltering }, sizeMultiplier);
}

void BloomPass::InitializeStates()
{
    pipelineStates_ = CachedStates{};
    pipelineStates_->bright_ = renderBufferManager_->CreateQuadPipelineState(BLEND_REPLACE, "v2/P_Bloom", "BRIGHT");
    pipelineStates_->blurH_ = renderBufferManager_->CreateQuadPipelineState(BLEND_REPLACE, "v2/P_Bloom", "BLURH");
    pipelineStates_->blurV_ = renderBufferManager_->CreateQuadPipelineState(BLEND_REPLACE, "v2/P_Bloom", "BLURV");
    pipelineStates_->bloom_ = renderBufferManager_->CreateQuadPipelineState(BLEND_REPLACE, "v2/P_Bloom", "COMBINE");
}

void BloomPass::EvaluateBloom()
{
    Texture2D* viewportTexture = renderBufferManager_->GetSecondaryColorTexture();
    const Vector2 invInputSize = Vector2::ONE / static_cast<Vector2>(viewportTexture->GetSize());
    const Vector2 invBlueSize = Vector2::ONE / static_cast<Vector2>(textures_.blurH_->GetTexture2D()->GetSize());
    const Color luminanceWeights = renderBufferManager_->GetSettings().colorSpace_ == RenderPipelineColorSpace::GammaLDR
        ? Color::LUMINOSITY_GAMMA : Color::LUMINOSITY_LINEAR;

    ShaderResourceDesc shaderResources[1];
    shaderResources[0].unit_ = TU_DIFFUSE;
    ShaderParameterDesc shaderParameters[] = {
        { "LuminanceWeights", luminanceWeights.ToVector3() },
        { "BloomThreshold", settings_.threshold_ },
        { "InputInvSize", invInputSize },
    };

    DrawQuadParams drawParams;
    drawParams.resources_ = shaderResources;
    drawParams.parameters_ = shaderParameters;
    drawParams.clipToUVOffsetAndScale_ = renderBufferManager_->GetDefaultClipToUVSpaceOffsetAndScale();

    shaderResources[0].texture_ = viewportTexture;
    drawParams.pipelineState_ = pipelineStates_->bright_;
    renderBufferManager_->SetRenderTargets(nullptr, { textures_.blurV_ });
    renderBufferManager_->DrawQuad("Gather bright regions", drawParams);

    shaderParameters[2].value_ = invBlueSize;
    shaderResources[0].texture_ = textures_.blurV_->GetTexture2D();
    drawParams.pipelineState_ = pipelineStates_->blurH_;
    renderBufferManager_->SetRenderTargets(nullptr, { textures_.blurH_ });
    renderBufferManager_->DrawQuad("Blur vertically", drawParams);

    shaderResources[0].texture_ = textures_.blurH_->GetTexture2D();
    drawParams.pipelineState_ = pipelineStates_->blurV_;
    renderBufferManager_->SetRenderTargets(nullptr, { textures_.blurV_ });
    renderBufferManager_->DrawQuad("Blur horizontally", drawParams);
}

void BloomPass::Execute()
{
    if (!pipelineStates_)
        InitializeStates();

    if (!pipelineStates_->IsValid())
        return;

    renderBufferManager_->PrepareForColorReadWrite(false);

    EvaluateBloom();

    const ShaderResourceDesc shaderResources[] = {
        { TU_NORMAL, textures_.blurV_->GetTexture2D() }
    };
    const ShaderParameterDesc shaderParameters[] = {
        { "BloomMix", Vector2(settings_.sourceIntensity_, settings_.bloomIntensity_) },
    };
    renderBufferManager_->SetOutputRenderTargers();
    renderBufferManager_->DrawFeedbackViewportQuad("Apply bloom", pipelineStates_->bloom_, shaderResources, shaderParameters);
}

}
