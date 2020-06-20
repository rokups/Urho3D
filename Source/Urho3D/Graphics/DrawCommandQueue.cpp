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
    // Constant buffers to store all shader parameters for queue
    ea::vector<SharedPtr<ConstantBuffer>> constantBuffers;

    // Utility to set shader parameters if constant buffers are not used
    const SharedParameterSetter shaderParameterSetter{ graphics };

    // Prepare shader parameters
    if (useConstantBuffers_)
    {
        const unsigned numConstantBuffers = constantBuffers_.collection_.GetNumBuffers();
        constantBuffers.resize(numConstantBuffers);
        for (unsigned i = 0; i < numConstantBuffers; ++i)
        {
            constantBuffers[i] = graphics->GetOrCreateConstantBuffer(VS, i, constantBuffers_.collection_.GetBufferSize(i));
            constantBuffers[i]->Update(constantBuffers_.collection_.GetBufferData(i));
        }
    }
    else
    {
        graphics->ClearParameterSources();
    }

    // Cached current state
    PipelineState* currentPipelineState = nullptr;
    IndexBuffer* currentIndexBuffer = nullptr;
    ea::array<VertexBuffer*, MAX_VERTEX_STREAMS> currentVertexBuffers{};
    ShaderResourceRange currentShaderResources;
    PrimitiveType currentPrimitiveType{};

    // Temporary collections
    ea::vector<VertexBuffer*> tempVertexBuffers;
    ea::array<ConstantBufferRange, MAX_SHADER_PARAMETER_GROUPS> constantBufferRanges{};

    for (const DrawCommandDescription& cmd : drawCommands_)
    {
        // Set pipeline state
        if (cmd.pipelineState_ != currentPipelineState)
        {
            cmd.pipelineState_->Apply();
            currentPipelineState = cmd.pipelineState_;
            currentPrimitiveType = currentPipelineState->GetDesc().primitiveType_;
        }

        // Set index buffer
        if (cmd.indexBuffer_ != currentIndexBuffer)
        {
            graphics->SetIndexBuffer(cmd.indexBuffer_);
            currentIndexBuffer = cmd.indexBuffer_;
        }

        // Set vertex buffers
        if (cmd.vertexBuffers_ != currentVertexBuffers || cmd.instanceCount_ != 0)
        {
            tempVertexBuffers.clear();
            tempVertexBuffers.assign(cmd.vertexBuffers_.begin(), cmd.vertexBuffers_.end());
            graphics->SetVertexBuffers(tempVertexBuffers, cmd.instanceStart_);
            currentVertexBuffers = cmd.vertexBuffers_;
        }

        // Set shader resources
        if (cmd.shaderResources_ != currentShaderResources)
        {
            for (unsigned i = cmd.shaderResources_.first; i < cmd.shaderResources_.second; ++i)
            {
                const auto& unitAndResource = shaderResources_[i];
                if (graphics->HasTextureUnit(unitAndResource.first))
                    graphics->SetTexture(unitAndResource.first, unitAndResource.second);
            }
            currentShaderResources = cmd.shaderResources_;
        }

        // Set shader parameters or constant buffers
        if (useConstantBuffers_)
        {
            // Update used ranges for each group
            for (unsigned i = 0; i < MAX_SHADER_PARAMETER_GROUPS; ++i)
            {
                // If constant buffer is not needed, ignore
                if (cmd.constantBuffers_[i].size_ == 0)
                    continue;

                constantBufferRanges[i].constantBuffer_ = constantBuffers[cmd.constantBuffers_[i].index_];
                constantBufferRanges[i].offset_ = cmd.constantBuffers_[i].offset_;
                constantBufferRanges[i].size_ = cmd.constantBuffers_[i].size_;
            }

            // Set all constant buffers at once
            graphics->SetShaderConstantBuffers(constantBufferRanges);
        }
        else
        {
            // Set parameters for each group if update needed
            for (unsigned i = 0; i < MAX_SHADER_PARAMETER_GROUPS; ++i)
            {
                const auto group = static_cast<ShaderParameterGroup>(i);
                const auto range = cmd.shaderParameters_[i];

                // If needed range is already bound to active shader program, ignore
                if (!graphics->NeedParameterUpdate(group, reinterpret_cast<void *>(range.first)))
                    continue;

                shaderParameters_.collection_.ForEach(range.first, range.second, shaderParameterSetter);
            }
        }

        // Invoke appropriate draw command
        if (cmd.instanceCount_ != 0)
        {
            if (cmd.baseVertexIndex_ == 0)
            {
                graphics->DrawInstanced(currentPrimitiveType,
                    cmd.indexStart_, cmd.indexCount_, 0, 0, cmd.instanceCount_);
            }
            else
            {
                graphics->DrawInstanced(currentPrimitiveType,
                    cmd.indexStart_, cmd.indexCount_, cmd.baseVertexIndex_, 0, 0, cmd.instanceCount_);
            }
        }
        else
        {
            if (!currentIndexBuffer)
                graphics->Draw(currentPrimitiveType, cmd.indexStart_, cmd.indexCount_);
            else if (cmd.baseVertexIndex_ == 0)
                graphics->Draw(currentPrimitiveType, cmd.indexStart_, cmd.indexCount_, 0, 0);
            else
                graphics->Draw(currentPrimitiveType, cmd.indexStart_, cmd.indexCount_, cmd.baseVertexIndex_, 0, 0);
        }
    }
}

}
