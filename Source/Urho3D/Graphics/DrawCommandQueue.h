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

#include "../Graphics/ConstantBuffer.h"
#include "../Graphics/ConstantBufferLayout.h"
#include "../Graphics/Geometry.h"
#include "../Graphics/IndexBuffer.h"
#include "../Graphics/PipelineState.h"
#include "../Graphics/ShaderParameterCollection.h"
#include "../Graphics/ConstantBufferCollection.h"

namespace Urho3D
{

/// Collection of shader resources.
using ShaderResourceCollection = ea::vector<ea::pair<TextureUnit, Texture*>>;

/// Shader parameter group, range in array.
using ShaderParameterRange = ea::pair<unsigned, unsigned>;

/// Shader resource group, range in array.
using ShaderResourceRange = ea::pair<unsigned, unsigned>;

/// Description of draw command.
struct DrawCommandDescription
{
    /// Pipeline state.
    PipelineState* pipelineState_{};
    /// Index buffer.
    IndexBuffer* indexBuffer_{};
    /// Vertex buffers.
    ea::array<VertexBuffer*, MAX_VERTEX_STREAMS> vertexBuffers_{};

    /// Shader parameters bound to the command.
    ea::array<ShaderParameterRange, MAX_SHADER_PARAMETER_GROUPS> shaderParameters_{};
    /// Constant buffers bound to the command.
    ea::array<ConstantBufferRef, MAX_SHADER_PARAMETER_GROUPS> constantBuffers_{};
    /// Shader resources bound to the command.
    ShaderResourceRange shaderResources_;

    /// Start vertex/index.
    unsigned indexStart_{};
    /// Number of vertices/indices.
    unsigned indexCount_{};
    /// Base vertex.
    unsigned baseVertexIndex_{};
    /// Start instance.
    unsigned instanceStart_{};
    /// Number of instances.
    unsigned instanceCount_{};
};

/// Queue of draw commands.
class DrawCommandQueue
{
public:
    /// Reset queue.
    void Reset(Graphics* graphics)
    {
        useConstantBuffers_ = graphics->GetConstantBuffersEnabled();

        shaderParameters_.Clear();
        constantBuffers_.Clear(graphics->GetConstantBuffersOffsetAlignment());
        shaderResources_.clear();
        drawOps_.clear();

        currentShaderParameterGroup_ = {};
        currentShaderResourceGroup_ = {};
        currentDrawOp_ = {};
        memset(currentConstantBufferHashes_, 0, sizeof(currentConstantBufferHashes_));
    }

    /// Set pipeline state.
    void SetPipelineState(PipelineState* pipelineState)
    {
        currentDrawOp_.pipelineState_ = pipelineState;
        currentConstantBufferLayout_ = pipelineState->GetDesc().constantBufferLayout_;
    }

    /// Begin shader parameter group.
    bool BeginShaderParameterGroup(ShaderParameterGroup group, bool force = false)
    {
        if (useConstantBuffers_)
        {
            const unsigned currentHash = currentConstantBufferLayout_->GetConstantBufferHash(group);
            if (force || currentHash != currentConstantBufferHashes_[group])
            {
                const unsigned size = currentConstantBufferLayout_->GetConstantBufferSize(group);
                const auto& buffer = constantBuffers_.AddBlock(size);

                currentDrawOp_.constantBuffers_[group] = buffer.first;
                currentConstantBufferData_ = buffer.second;
                currentConstantBufferHashes_[group] = currentHash;
                return true;
            }
            return false;
        }
        else
        {
            return force || currentDrawOp_.shaderParameters_[group].first == currentDrawOp_.shaderParameters_[group].second;
        }
    }

    /// Add shader parameter.
    template <class T>
    void AddShaderParameter(StringHash name, const T& value)
    {
        if (useConstantBuffers_)
        {
            const auto paramInfo = currentConstantBufferLayout_->GetConstantBufferParameter(name);
            if (paramInfo.second != M_MAX_UNSIGNED)
                ConstantBufferCollection::StoreParameter(currentConstantBufferData_ + paramInfo.second, value);
        }
        else
        {
            shaderParameters_.AddParameter(name, value);
            ++currentShaderParameterGroup_.second;
        }
    }

    /// Commit shader parameter group.
    void CommitShaderParameterGroup(ShaderParameterGroup group)
    {
        if (useConstantBuffers_)
        {
        }
        else
        {
            currentDrawOp_.shaderParameters_[static_cast<unsigned>(group)] = currentShaderParameterGroup_;
            currentShaderParameterGroup_.first = shaderParameters_.Size();
            currentShaderParameterGroup_.second = currentShaderParameterGroup_.first;
        }
    }

    /// Add shader resource.
    void AddShaderResource(TextureUnit unit, Texture* texture)
    {
        shaderResources_.emplace_back(unit, texture);
        ++currentShaderResourceGroup_.second;
    }

    /// Commit shader resource group.
    void CommitShaderResourceGroup()
    {
        currentDrawOp_.shaderResources_ = currentShaderResourceGroup_;
        currentShaderResourceGroup_.first = shaderResources_.size();
        currentShaderResourceGroup_.second = currentShaderResourceGroup_.first;
    }

    /// Set buffers.
    void SetBuffers(ea::span<VertexBuffer*> vertexBuffers, IndexBuffer* indexBuffer)
    {
        ea::copy(vertexBuffers.begin(), vertexBuffers.end(), currentDrawOp_.vertexBuffers_.begin());
        currentDrawOp_.indexBuffer_ = indexBuffer;
    }

    /// Set buffers.
    void SetBuffers(const ea::vector<SharedPtr<VertexBuffer>>& vertexBuffers, IndexBuffer* indexBuffer)
    {
        ea::copy(vertexBuffers.begin(), vertexBuffers.end(), currentDrawOp_.vertexBuffers_.begin());
        currentDrawOp_.indexBuffer_ = indexBuffer;
    }

    /// Draw non-indexed geometry.
    void Draw(unsigned vertexStart, unsigned vertexCount)
    {
        currentDrawOp_.indexBuffer_ = nullptr;
        currentDrawOp_.indexStart_ = vertexStart;
        currentDrawOp_.indexCount_ = vertexCount;
        currentDrawOp_.baseVertexIndex_ = 0;
        currentDrawOp_.instanceStart_ = 0;
        currentDrawOp_.instanceCount_ = 0;
        drawOps_.push_back(currentDrawOp_);
    }

    /// Draw indexed geometry.
    void DrawIndexed(unsigned indexStart, unsigned indexCount)
    {
        assert(currentDrawOp_.indexBuffer_);
        currentDrawOp_.indexStart_ = indexStart;
        currentDrawOp_.indexCount_ = indexCount;
        currentDrawOp_.baseVertexIndex_ = 0;
        currentDrawOp_.instanceStart_ = 0;
        currentDrawOp_.instanceCount_ = 0;
        drawOps_.push_back(currentDrawOp_);
    }

    /// Draw indexed geometry with vertex index offset.
    void DrawIndexed(unsigned indexStart, unsigned indexCount, unsigned baseVertexIndex)
    {
        assert(currentDrawOp_.indexBuffer_);
        currentDrawOp_.indexStart_ = indexStart;
        currentDrawOp_.indexCount_ = indexCount;
        currentDrawOp_.baseVertexIndex_ = baseVertexIndex;
        currentDrawOp_.instanceStart_ = 0;
        currentDrawOp_.instanceCount_ = 0;
        drawOps_.push_back(currentDrawOp_);
    }

    /// Draw indexed, instanced geometry.
    void DrawIndexedInstanced(unsigned indexStart, unsigned indexCount, unsigned instanceStart, unsigned instanceCount)
    {
        assert(currentDrawOp_.indexBuffer_);
        currentDrawOp_.indexStart_ = indexStart;
        currentDrawOp_.indexCount_ = indexCount;
        currentDrawOp_.baseVertexIndex_ = 0;
        currentDrawOp_.instanceStart_ = instanceStart;
        currentDrawOp_.instanceCount_ = instanceCount;
        drawOps_.push_back(currentDrawOp_);
    }

    /// Draw indexed, instanced geometry with vertex index offset.
    void DrawIndexedInstanced(unsigned indexStart, unsigned indexCount, unsigned baseVertexIndex,
        unsigned instanceStart, unsigned instanceCount)
    {
        assert(currentDrawOp_.indexBuffer_);
        currentDrawOp_.indexStart_ = indexStart;
        currentDrawOp_.indexCount_ = indexCount;
        currentDrawOp_.baseVertexIndex_ = baseVertexIndex;
        currentDrawOp_.instanceStart_ = instanceStart;
        currentDrawOp_.instanceCount_ = instanceCount;
        drawOps_.push_back(currentDrawOp_);
    }

    /// Execute commands in the queue.
    void Execute(Graphics* graphics);

private:
    /// Whether to use constant buffers.
    bool useConstantBuffers_{};

    /// Shader parameters
    ShaderParameterCollection shaderParameters_;
    /// Constant buffers.
    ConstantBufferCollection constantBuffers_;

    /// Shader resources.
    ShaderResourceCollection shaderResources_;
    /// Draw operations.
    ea::vector<DrawCommandDescription> drawOps_;

    /// Current draw operation.
    DrawCommandDescription currentDrawOp_;
    /// Current shader parameter group.
    ShaderParameterRange currentShaderParameterGroup_;
    /// Current shader resource group.
    ShaderResourceRange currentShaderResourceGroup_;

    /// Current constant buffer layout.
    ConstantBufferLayout* currentConstantBufferLayout_{};
    /// Current pointer to constant buffer data.
    unsigned char* currentConstantBufferData_{};
    /// Current constant buffer layout hashes.
    unsigned currentConstantBufferHashes_[MAX_SHADER_PARAMETER_GROUPS]{};
};

}
