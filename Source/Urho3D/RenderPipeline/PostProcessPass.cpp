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
    , debugComment_(Format("Apply shader '{}'", shaderName))
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
        renderBufferManager_->SwapColorBuffers(false);
    renderBufferManager_->SetOutputRenderTargers();

    if (colorReadAndWrite)
        renderBufferManager_->DrawFeedbackViewportQuad(debugComment_, pipelineState_, shaderResources_, shaderParameters_);
    else
        renderBufferManager_->DrawViewportQuad(debugComment_, pipelineState_, shaderResources_, shaderParameters_);
}

}
