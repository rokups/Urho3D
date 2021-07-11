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

#include <EASTL/numeric.h>

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
        const bool resetCachedTextures = settings_.numIterations_ != settings.numIterations_
            || settings_.hdr_ != settings.hdr_;
        settings_ = settings;
        if (resetCachedTextures)
            InitializeTextures();
    }
}

void BloomPass::InitializeTextures()
{
    textures_.clear();
    textures_.resize(settings_.numIterations_);

    for (unsigned i = 0; i < settings_.numIterations_; ++i)
    {
        const unsigned format = settings_.hdr_ ? Graphics::GetRGBAFloat16Format() : Graphics::GetRGBFormat();
        const RenderBufferParams params{ format, 1, RenderBufferFlag::BilinearFiltering };
        // Start scale is 1
        // It's okay to do it unchecked because render buffers are never smaller than 1x1
        const Vector2 sizeMultiplier = Vector2::ONE / static_cast<float>(1 << i);
        textures_[i].final_ = renderBufferManager_->CreateColorBuffer(params, sizeMultiplier);
        textures_[i].temporary_ = renderBufferManager_->CreateColorBuffer(params, sizeMultiplier);
    }
}

void BloomPass::InitializeStates()
{
    pipelineStates_ = CachedStates{};
    pipelineStates_->bright_ = renderBufferManager_->CreateQuadPipelineState(BLEND_REPLACE, "v2/P_Bloom", "BRIGHT");
    pipelineStates_->blurH_ = renderBufferManager_->CreateQuadPipelineState(BLEND_REPLACE, "v2/P_Bloom", "BLURH");
    pipelineStates_->blurV_ = renderBufferManager_->CreateQuadPipelineState(BLEND_REPLACE, "v2/P_Bloom", "BLURV");
    pipelineStates_->bloom_ = renderBufferManager_->CreateQuadPipelineState(BLEND_PREMULALPHA, "v2/P_Bloom", "COMBINE");
}

unsigned BloomPass::GatherBrightRegions(RenderBuffer* destination)
{
    Texture2D* viewportTexture = renderBufferManager_->GetSecondaryColorTexture();
    const IntVector2 inputSize = viewportTexture->GetSize();
    const Vector2 inputInvSize = Vector2::ONE / static_cast<Vector2>(inputSize);

    const ShaderResourceDesc shaderResources[] = { { TU_DIFFUSE, viewportTexture } };
    const auto shaderParameters = GetShaderParameters(inputInvSize);

    DrawQuadParams drawParams;
    drawParams.resources_ = shaderResources;
    drawParams.parameters_ = shaderParameters;
    drawParams.clipToUVOffsetAndScale_ = renderBufferManager_->GetDefaultClipToUVSpaceOffsetAndScale();

    drawParams.pipelineState_ = pipelineStates_->bright_;
    renderBufferManager_->SetRenderTargets(nullptr, { destination });
    renderBufferManager_->DrawQuad("Gather bright regions", drawParams);

    return Clamp(LogBaseTwo(ea::min(inputSize.x_, inputSize.y_)), 1u, settings_.numIterations_);
}

void BloomPass::BlurTexture(RenderBuffer* final, RenderBuffer* temporary)
{
    ShaderResourceDesc shaderResources[1];
    shaderResources[0].unit_ = TU_DIFFUSE;

    const Vector2 inputInvSize = Vector2::ONE / static_cast<Vector2>(final->GetTexture2D()->GetSize());
    const auto shaderParameters = GetShaderParameters(inputInvSize);

    DrawQuadParams drawParams;
    drawParams.resources_ = shaderResources;
    drawParams.parameters_ = shaderParameters;
    drawParams.clipToUVOffsetAndScale_ = renderBufferManager_->GetDefaultClipToUVSpaceOffsetAndScale();

    shaderResources[0].texture_ = final->GetTexture2D();
    drawParams.pipelineState_ = pipelineStates_->blurH_;
    renderBufferManager_->SetRenderTargets(nullptr, { temporary });
    renderBufferManager_->DrawQuad("Blur vertically", drawParams);

    shaderResources[0].texture_ = temporary->GetTexture2D();
    drawParams.pipelineState_ = pipelineStates_->blurV_;
    renderBufferManager_->SetRenderTargets(nullptr, { final });
    renderBufferManager_->DrawQuad("Blur horizontally", drawParams);
}

void BloomPass::ApplyBloom(RenderBuffer* bloom, float intensity)
{
    const ShaderResourceDesc shaderResources[] = {
        { TU_DIFFUSE, bloom->GetTexture2D() }
    };
    const ShaderParameterDesc shaderParameters[] = {
        { Bloom_LuminanceWeights, luminanceWeights_ },
        { Bloom_Intensity, intensity }
    };
    renderBufferManager_->DrawViewportQuad("Apply bloom", pipelineStates_->bloom_, shaderResources, shaderParameters);
}

void BloomPass::CopyTexture(RenderBuffer* source, RenderBuffer* destination)
{
    renderBufferManager_->SetRenderTargets(nullptr, { destination });
    renderBufferManager_->DrawTexture("Downscale bloom", source->GetTexture2D());
}

void BloomPass::Execute()
{
    if (!pipelineStates_)
        InitializeStates();

    if (!pipelineStates_->IsValid())
        return;

    luminanceWeights_ = renderBufferManager_->GetSettings().colorSpace_ == RenderPipelineColorSpace::GammaLDR
        ? Color::LUMINOSITY_GAMMA.ToVector3() : Color::LUMINOSITY_LINEAR.ToVector3();

    renderBufferManager_->SwapColorBuffers(false);

    const unsigned numIterations = GatherBrightRegions(textures_[0].final_);
    for (unsigned i = 0; i < numIterations; ++i)
    {
        if (i > 0)
            CopyTexture(textures_[i - 1].final_, textures_[i].final_);
        BlurTexture(textures_[i].final_, textures_[i].temporary_);
    }

    intensityMultipliers_.resize(numIterations);
    for (unsigned i = 0; i < numIterations; ++i)
        intensityMultipliers_[i] = Pow(settings_.iterationFactor_, static_cast<float>(i));
    const float totalIntensity = ea::accumulate(intensityMultipliers_.begin(), intensityMultipliers_.end(), 0.0f);
    for (unsigned i = 0; i < numIterations; ++i)
        intensityMultipliers_[i] *= settings_.intensity_ / totalIntensity;

    renderBufferManager_->SwapColorBuffers(false);
    renderBufferManager_->SetOutputRenderTargers();
    for (unsigned i = 0; i < numIterations; ++i)
        ApplyBloom(textures_[i].final_, intensityMultipliers_[i]);
}

}
