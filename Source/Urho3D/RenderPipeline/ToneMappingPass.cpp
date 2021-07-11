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
}

void ToneMappingPass::SetMode(ToneMappingMode mode)
{
    if (mode_ != mode)
    {
        toneMappingState_ = nullptr;
        mode_ = mode;
    }
}

void ToneMappingPass::InitializeStates()
{
    ea::string defines;
    switch (mode_)
    {
    case ToneMappingMode::Reinhard:
        defines += "REINHARD ";
        break;
    case ToneMappingMode::ReinhardWhite:
        defines += "REINHARDWHITE ";
        break;
    case ToneMappingMode::Uncharted2:
        defines += "UNCHARTED2 ";
        break;
    default:
        break;
    }

    toneMappingState_  = renderBufferManager_->CreateQuadPipelineState(
        BLEND_REPLACE, "v2/P_ToneMapping", defines);
}

void ToneMappingPass::Execute()
{
    if (!toneMappingState_)
        InitializeStates();

    if (!toneMappingState_->IsValid())
        return;

    renderBufferManager_->SwapColorBuffers(false);

    renderBufferManager_->SetOutputRenderTargers();
    renderBufferManager_->DrawFeedbackViewportQuad("Apply tone mapping", toneMappingState_, {}, {});
}

}
