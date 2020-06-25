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

#include "../Graphics/PipelineState.h"

#include "../Core/Context.h"
#include "../Graphics/Graphics.h"

namespace Urho3D
{

PipelineState::PipelineState(Context* context)
    : Object(context)
    , graphics_(context_->GetGraphics())
{
}

void PipelineState::Create(const PipelineStateDesc& desc)
{
    desc_ = desc;
    desc_.constantBufferLayout_ = graphics_->GetConstantBufferLayout(desc_.vertexShader_, desc_.pixelShader_);
}

void PipelineState::Apply()
{
    graphics_->SetShaders(desc_.vertexShader_, desc_.pixelShader_);
    graphics_->SetDepthWrite(desc_.depthWrite_);
    graphics_->SetDepthTest(desc_.depthMode_);
    graphics_->SetStencilTest(desc_.stencilEnabled_, desc_.stencilMode_,
        desc_.stencilPass_, desc_.stencilFail_, desc_.stencilDepthFail_,
        desc_.stencilRef_, desc_.compareMask_, desc_.writeMask_);

    graphics_->SetColorWrite(desc_.colorWrite_);
    graphics_->SetBlendMode(desc_.blendMode_, desc_.alphaToCoverage_);

    graphics_->SetFillMode(desc_.fillMode_);
    graphics_->SetCullMode(desc_.cullMode_);
    graphics_->SetDepthBias(desc_.constantDepthBias_, desc_.slopeScaledDepthBias_);
}

}
