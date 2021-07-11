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

DrawCommandQueue::DrawCommandQueue(Graphics* graphics)
    : graphics_(graphics)
{

}

void DrawCommandQueue::Reset(bool preferConstantBuffers)
{
    useConstantBuffers_ = preferConstantBuffers
        ? graphics_->GetCaps().constantBuffersSupported_
        : !graphics_->GetCaps().globalUniformsSupported_;

    // Reset state accumulators
    currentDrawCommand_ = {};
    currentShaderResourceGroup_ = {};

    // Clear shadep parameters
    if (useConstantBuffers_)
    {
        constantBuffers_.collection_.ClearAndInitialize(graphics_->GetCaps().constantBufferOffsetAlignment_);
        constantBuffers_.currentLayout_ = nullptr;
        constantBuffers_.currentData_ = nullptr;
        constantBuffers_.currentHashes_.fill(0);

        currentDrawCommand_.constantBuffers_.fill({});
    }
    else
    {
        shaderParameters_.collection_.Clear();
        shaderParameters_.currentGroupRange_ = {};

        currentDrawCommand_.shaderParameters_.fill({});
    }

    // Clear arrays and draw commands
    shaderResources_.clear();
    drawCommands_.clear();
    scissorRects_.clear();
    scissorRects_.push_back(IntRect::ZERO);
}

void DrawCommandQueue::Execute()
{
    if (drawCommands_.empty())
        return;

    // Constant buffers to store all shader parameters for queue
    ea::vector<SharedPtr<ConstantBuffer>> constantBuffers;

    // Utility to set shader parameters if constant buffers are not used
    const SharedParameterSetter shaderParameterSetter{ graphics_ };

    // Prepare shader parameters
    if (useConstantBuffers_)
    {
        const unsigned numConstantBuffers = constantBuffers_.collection_.GetNumBuffers();
        constantBuffers.resize(numConstantBuffers);
        for (unsigned i = 0; i < numConstantBuffers; ++i)
        {
            constantBuffers[i] = graphics_->GetOrCreateConstantBuffer(VS, i, constantBuffers_.collection_.GetGPUBufferSize(i));
            constantBuffers[i]->Update(constantBuffers_.collection_.GetBufferData(i));
        }
    }
    else
    {
        graphics_->ClearParameterSources();
    }

    // Cached current state
    PipelineState* currentPipelineState = nullptr;
    IndexBuffer* currentIndexBuffer = nullptr;
    ea::array<VertexBuffer*, MAX_VERTEX_STREAMS> currentVertexBuffers{};
    ShaderResourceRange currentShaderResources;
    PrimitiveType currentPrimitiveType{};
    unsigned currentScissorRect = M_MAX_UNSIGNED;

    // Temporary collections
    ea::vector<VertexBuffer*> tempVertexBuffers;
    ea::array<ConstantBufferRange, MAX_SHADER_PARAMETER_GROUPS> constantBufferRanges{};

    for (const DrawCommandDescription& cmd : drawCommands_)
    {
        // Set pipeline state
        if (cmd.pipelineState_ != currentPipelineState)
        {
            cmd.pipelineState_->Apply(graphics_);
            currentPipelineState = cmd.pipelineState_;
            currentPrimitiveType = currentPipelineState->GetDesc().primitiveType_;
            // Reset current shader resources because of HasTextureUnit check below
            currentShaderResources = {};
        }

        // Set scissor
        if (cmd.scissorRect_ != currentScissorRect)
        {
            const bool scissorEnabled = currentPipelineState->GetDesc().scissorTestEnabled_;
            graphics_->SetScissorTest(scissorEnabled, scissorRects_[cmd.scissorRect_]);
            currentScissorRect = cmd.scissorRect_;
        }

        // Set index buffer
        if (cmd.inputBuffers_.indexBuffer_ != currentIndexBuffer)
        {
            graphics_->SetIndexBuffer(cmd.inputBuffers_.indexBuffer_);
            currentIndexBuffer = cmd.inputBuffers_.indexBuffer_;
        }

        // Set vertex buffers
        if (cmd.inputBuffers_.vertexBuffers_ != currentVertexBuffers || cmd.instanceCount_ != 0)
        {
            tempVertexBuffers.clear();
            tempVertexBuffers.assign(cmd.inputBuffers_.vertexBuffers_.begin(), cmd.inputBuffers_.vertexBuffers_.end());
            graphics_->SetVertexBuffers(tempVertexBuffers, cmd.instanceStart_);
            currentVertexBuffers = cmd.inputBuffers_.vertexBuffers_;
        }

        // Set shader resources
        if (cmd.shaderResources_ != currentShaderResources)
        {
            for (unsigned i = cmd.shaderResources_.first; i < cmd.shaderResources_.second; ++i)
            {
                const auto& unitAndResource = shaderResources_[i];
                if (graphics_->HasTextureUnit(unitAndResource.unit_))
                    graphics_->SetTexture(unitAndResource.unit_, unitAndResource.texture_);
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
            graphics_->SetShaderConstantBuffers(constantBufferRanges);
        }
        else
        {
            // Set parameters for each group if update needed
            for (unsigned i = 0; i < MAX_SHADER_PARAMETER_GROUPS; ++i)
            {
                const auto group = static_cast<ShaderParameterGroup>(i);
                const auto range = cmd.shaderParameters_[i];

                // If needed range is already bound to active shader program, ignore
                if (!graphics_->NeedParameterUpdate(group, reinterpret_cast<void *>(static_cast<uintptr_t>(range.first))))
                    continue;

                shaderParameters_.collection_.ForEach(range.first, range.second, shaderParameterSetter);
            }
        }

        // Invoke appropriate draw command
#ifdef URHO3D_D3D9
        const unsigned vertexStart = cmd.vertexStart_;
        const unsigned vertexCount = cmd.vertexCount_;
#else
        const unsigned vertexStart = 0;
        const unsigned vertexCount = 0;
#endif
        if (cmd.instanceCount_ != 0)
        {
            if (cmd.baseVertexIndex_ == 0)
            {
                graphics_->DrawInstanced(currentPrimitiveType, cmd.indexStart_, cmd.indexCount_,
                    vertexStart, vertexCount, cmd.instanceCount_);
            }
            else
            {
                graphics_->DrawInstanced(currentPrimitiveType, cmd.indexStart_, cmd.indexCount_,
                    cmd.baseVertexIndex_, vertexStart, vertexCount, cmd.instanceCount_);
            }
        }
        else
        {
            if (!currentIndexBuffer)
            {
                graphics_->Draw(currentPrimitiveType, cmd.indexStart_, cmd.indexCount_);
            }
            else if (cmd.baseVertexIndex_ == 0)
            {
                graphics_->Draw(currentPrimitiveType, cmd.indexStart_, cmd.indexCount_,
                    vertexStart, vertexCount);
            }
            else
            {
                graphics_->Draw(currentPrimitiveType, cmd.indexStart_, cmd.indexCount_,
                    cmd.baseVertexIndex_, vertexStart, vertexCount);
            }
        }
    }
}

}
