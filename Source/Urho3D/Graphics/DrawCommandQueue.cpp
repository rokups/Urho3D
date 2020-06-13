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
#include "../Graphics/DrawCommandQueue.h"

#include "../DebugNew.h"

namespace Urho3D
{

void DrawCommandQueue::Execute(Graphics* graphics)
{
    const unsigned numConstantBuffers = constantBuffers_.GetNumBuffers();
    ea::vector<SharedPtr<ConstantBuffer>> constantBuffers;
    constantBuffers.resize(numConstantBuffers);
    for (unsigned i = 0; i < numConstantBuffers; ++i)
    {
        constantBuffers[i] = graphics->GetOrCreateConstantBuffer(VS, i, constantBuffers_.GetBufferSize(i));
        constantBuffers[i]->Update(constantBuffers_.GetBufferData(i));
    }

    graphics->ClearParameterSources();

    const SharedParameterSetter shaderParameterSetter{ graphics };

    PipelineState* currentPipelineState = nullptr;
    IndexBuffer* currentIndexBuffer = nullptr;
    ea::array<VertexBuffer*, MAX_VERTEX_STREAMS> currentVertexBuffers{};
    unsigned currentShaderResources = M_MAX_UNSIGNED;
    PrimitiveType currentPrimitiveType{};

    ea::vector<VertexBuffer*> tempVertexBuffers;
    ConstantBufferRange constantBufferRanges[MAX_SHADER_PARAMETER_GROUPS]{};

    for (const DrawCommandDescription& drawOp : drawOps_)
    {
        // Apply pipeline state
        if (drawOp.pipelineState_ != currentPipelineState)
        {
            drawOp.pipelineState_->Apply();
            currentPipelineState = drawOp.pipelineState_;
            currentPrimitiveType = currentPipelineState->GetDesc().primitiveType_;
        }

        // Apply buffers
        if (drawOp.indexBuffer_ != currentIndexBuffer)
        {
            graphics->SetIndexBuffer(drawOp.indexBuffer_);
            currentIndexBuffer = drawOp.indexBuffer_;
        }

        if (drawOp.vertexBuffers_ != currentVertexBuffers || drawOp.instanceCount_ != 0)
        {
            tempVertexBuffers.clear();
            tempVertexBuffers.assign(drawOp.vertexBuffers_.begin(), drawOp.vertexBuffers_.end());
            graphics->SetVertexBuffers(tempVertexBuffers, drawOp.instanceStart_);
            currentVertexBuffers = drawOp.vertexBuffers_;
        }

        // Apply shader resources
        if (drawOp.shaderResources_.first != currentShaderResources)
        {
            for (unsigned i = drawOp.shaderResources_.first; i < drawOp.shaderResources_.second; ++i)
            {
                const auto& unitAndResource = shaderResources_[i];
                if (graphics->HasTextureUnit(unitAndResource.first))
                    graphics->SetTexture(unitAndResource.first, unitAndResource.second);
            }
            currentShaderResources = drawOp.shaderResources_.first;
        }

        // Apply shader parameters
        for (unsigned i = 0; i < MAX_SHADER_PARAMETER_GROUPS; ++i)
        {
            const auto group = static_cast<ShaderParameterGroup>(i);
            if (useConstantBuffers_)
            {
                if (drawOp.constantBuffers_[i].size_ != 0)
                {
                    constantBufferRanges[i].constantBuffer_ = constantBuffers[drawOp.constantBuffers_[i].constantBufferIndex_];
                    constantBufferRanges[i].offset_ = drawOp.constantBuffers_[i].offset_;
                    constantBufferRanges[i].size_ = drawOp.constantBuffers_[i].size_;
                }
                else
                {
                    constantBufferRanges[i] = ConstantBufferRange{};
                }
            }
            else
            {
                const auto range = drawOp.shaderParameters_[i];
                if (!graphics->NeedParameterUpdate(group, reinterpret_cast<void*>(range.first)))
                    continue;

                shaderParameters_.ForEach(range.first, range.second, shaderParameterSetter);
            }
        }
        if (useConstantBuffers_)
        {
            graphics->SetShaderConstantBuffers(constantBufferRanges);
        }

        // Draw
        if (drawOp.instanceCount_ != 0)
        {
            if (drawOp.baseVertexIndex_ == 0)
            {
                graphics->DrawInstanced(currentPrimitiveType,
                    drawOp.indexStart_, drawOp.indexCount_, 0, 0, drawOp.instanceCount_);
            }
            else
            {
                graphics->DrawInstanced(currentPrimitiveType,
                    drawOp.indexStart_, drawOp.indexCount_, drawOp.baseVertexIndex_, 0, 0, drawOp.instanceCount_);
            }
        }
        else
        {
            if (!currentIndexBuffer)
                graphics->Draw(currentPrimitiveType, drawOp.indexStart_, drawOp.indexCount_);
            else if (drawOp.baseVertexIndex_ == 0)
                graphics->Draw(currentPrimitiveType, drawOp.indexStart_, drawOp.indexCount_, 0, 0);
            else
                graphics->Draw(currentPrimitiveType, drawOp.indexStart_, drawOp.indexCount_, drawOp.baseVertexIndex_, 0, 0);
        }
    }
}

}
