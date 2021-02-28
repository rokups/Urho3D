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

/*#include "../Core/Context.h"
#include "../Core/IteratorRange.h"
#include "../Graphics/Camera.h"
#include "../Graphics/ConstantBuffer.h"
#include "../Graphics/ShaderProgramLayout.h"
#include "../Graphics/DebugRenderer.h"
#include "../Graphics/DrawCommandQueue.h"
#include "../Graphics/IndexBuffer.h"
#include "../Graphics/OcclusionBuffer.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/Viewport.h"
#include "../RenderPipeline/RenderPipeline.h"*/
#include "../Graphics/DrawCommandQueue.h"
#include "../RenderPipeline/RenderBufferManager.h"
#include "../RenderPipeline/RenderPipelineInterface.h"
#include "../RenderPipeline/PostProcessPass.h"

#include "../DebugNew.h"

namespace Urho3D
{

PostProcessPass::PostProcessPass(RenderPipelineInterface* renderPipeline, RenderBufferManager* renderBufferManager)
    : Serializable(renderPipeline->GetContext())
    , renderBufferManager_(renderBufferManager)
{
    pipelineState_ = renderBufferManager_->CreateQuadPipelineState(BLEND_REPLACE, "v2/FXAA2", "");
}

PostProcessPass::~PostProcessPass()
{
}

void PostProcessPass::RegisterObject(Context* context)
{

}

void PostProcessPass::ApplyAttributes()
{

}

void PostProcessPass::Execute()
{
    if (!pipelineState_)
        return;

    renderBufferManager_->PrepareForColorReadWrite(false);
    renderBufferManager_->SetOutputRenderTargers();

    ShaderParameterDesc shaderParameters[] = { { "FXAAParams", Vector3(0.4, 0.5, 0.75) } };

    DrawQuadParams params;
    params.pipelineState_ = pipelineState_;
    params.bindSecondaryColorToDiffuse_ = true;
    params.parameters_ = shaderParameters;
    renderBufferManager_->DrawViewportQuad(params);
}

}
