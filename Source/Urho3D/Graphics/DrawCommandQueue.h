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

/// Shader parameter group, range in array. Plain old data.
struct ShaderParameterRange { unsigned first; unsigned second; };

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

    union
    {
        /// Shader parameters bound to the command.
        ea::array<ShaderParameterRange, MAX_SHADER_PARAMETER_GROUPS> shaderParameters_;
        /// Constant buffers bound to the command.
        ea::array<ConstantBufferCollectionRef, MAX_SHADER_PARAMETER_GROUPS> constantBuffers_;
    };

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

        // Reset state accumulators
        currentDrawCommand_ = {};
        currentShaderResourceGroup_ = {};

        // Clear shadep parameters
        if (useConstantBuffers_)
        {
            constantBuffers_.collection_.ClearAndInitialize(graphics->GetConstantBuffersOffsetAlignment());
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
    }

    /// Set pipeline state. Must be called first.
    void SetPipelineState(PipelineState* pipelineState)
    {
        currentDrawCommand_.pipelineState_ = pipelineState;

        if (useConstantBuffers_)
        {
            constantBuffers_.currentLayout_ = pipelineState->GetDesc().constantBufferLayout_;
        }
    }

    /// Begin shader parameter group. All parameters shall be set for each draw command.
    bool BeginShaderParameterGroup(ShaderParameterGroup group, bool differentFromPrevious = false)
    {
        if (useConstantBuffers_)
        {
            // Allocate new block if buffer layout is different or new data arrived
            const unsigned groupLayoutHash = constantBuffers_.currentLayout_->GetConstantBufferHash(group);
            if (differentFromPrevious || groupLayoutHash != constantBuffers_.currentHashes_[group])
            {
                const unsigned size = constantBuffers_.currentLayout_->GetConstantBufferSize(group);
                const auto& refAndData = constantBuffers_.collection_.AddBlock(size);

                currentDrawCommand_.constantBuffers_[group] = refAndData.first;
                constantBuffers_.currentData_ = refAndData.second;
                constantBuffers_.currentHashes_[group] = groupLayoutHash;
                return true;
            }
            return false;
        }
        else
        {
            // Allocate new group if different from previous or group is not initialized yet
            const ShaderParameterRange& groupRange = currentDrawCommand_.shaderParameters_[group];
            const bool groupInitialized = groupRange.first == groupRange.second;
            return differentFromPrevious || !groupInitialized;
        }
    }

    /// Add shader parameter. Shall be called only if BeginShaderParameterGroup returned true.
    template <class T>
    void AddShaderParameter(StringHash name, const T& value)
    {
        if (useConstantBuffers_)
        {
            const auto paramInfo = constantBuffers_.currentLayout_->GetConstantBufferParameter(name);
            if (paramInfo.second != M_MAX_UNSIGNED)
                ConstantBufferCollection::StoreParameter(constantBuffers_.currentData_ + paramInfo.second, value);
        }
        else
        {
            shaderParameters_.collection_.AddParameter(name, value);
            ++shaderParameters_.currentGroupRange_.second;
        }
    }

    /// Commit shader parameter group. Shall be called only if BeginShaderParameterGroup returned true.
    void CommitShaderParameterGroup(ShaderParameterGroup group)
    {
        if (useConstantBuffers_)
        {
            // All data is already stored, nothing to do
        }
        else
        {
            // Store range in draw op
            currentDrawCommand_.shaderParameters_[static_cast<unsigned>(group)] = shaderParameters_.currentGroupRange_;
            shaderParameters_.currentGroupRange_.first = shaderParameters_.collection_.Size();
            shaderParameters_.currentGroupRange_.second = shaderParameters_.currentGroupRange_.first;
        }
    }

    /// Add shader resource.
    void AddShaderResource(TextureUnit unit, Texture* texture)
    {
        shaderResources_.emplace_back(unit, texture);
        ++currentShaderResourceGroup_.second;
    }

    /// Commit shader resources added since previous commit.
    void CommitShaderResources()
    {
        currentDrawCommand_.shaderResources_ = currentShaderResourceGroup_;
        currentShaderResourceGroup_.first = shaderResources_.size();
        currentShaderResourceGroup_.second = currentShaderResourceGroup_.first;
    }

    /// Set vertex and index buffers.
    void SetBuffers(ea::span<VertexBuffer*> vertexBuffers, IndexBuffer* indexBuffer)
    {
        ea::copy(vertexBuffers.begin(), vertexBuffers.end(), currentDrawCommand_.vertexBuffers_.begin());
        currentDrawCommand_.indexBuffer_ = indexBuffer;
    }

    /// Set vertex and index buffers.
    void SetBuffers(const ea::vector<SharedPtr<VertexBuffer>>& vertexBuffers, IndexBuffer* indexBuffer)
    {
        ea::copy(vertexBuffers.begin(), vertexBuffers.end(), currentDrawCommand_.vertexBuffers_.begin());
        currentDrawCommand_.indexBuffer_ = indexBuffer;
    }

    /// Enqueue draw non-indexed geometry.
    void Draw(unsigned vertexStart, unsigned vertexCount)
    {
        currentDrawCommand_.indexBuffer_ = nullptr;
        currentDrawCommand_.indexStart_ = vertexStart;
        currentDrawCommand_.indexCount_ = vertexCount;
        currentDrawCommand_.baseVertexIndex_ = 0;
        currentDrawCommand_.instanceStart_ = 0;
        currentDrawCommand_.instanceCount_ = 0;
        drawCommands_.push_back(currentDrawCommand_);
    }

    /// Enqueue draw indexed geometry.
    void DrawIndexed(unsigned indexStart, unsigned indexCount)
    {
        assert(currentDrawCommand_.indexBuffer_);
        currentDrawCommand_.indexStart_ = indexStart;
        currentDrawCommand_.indexCount_ = indexCount;
        currentDrawCommand_.baseVertexIndex_ = 0;
        currentDrawCommand_.instanceStart_ = 0;
        currentDrawCommand_.instanceCount_ = 0;
        drawCommands_.push_back(currentDrawCommand_);
    }

    /// Enqueue draw indexed geometry with vertex index offset.
    void DrawIndexed(unsigned indexStart, unsigned indexCount, unsigned baseVertexIndex)
    {
        assert(currentDrawCommand_.indexBuffer_);
        currentDrawCommand_.indexStart_ = indexStart;
        currentDrawCommand_.indexCount_ = indexCount;
        currentDrawCommand_.baseVertexIndex_ = baseVertexIndex;
        currentDrawCommand_.instanceStart_ = 0;
        currentDrawCommand_.instanceCount_ = 0;
        drawCommands_.push_back(currentDrawCommand_);
    }

    /// Enqueue draw indexed, instanced geometry.
    void DrawIndexedInstanced(unsigned indexStart, unsigned indexCount, unsigned instanceStart, unsigned instanceCount)
    {
        assert(currentDrawCommand_.indexBuffer_);
        currentDrawCommand_.indexStart_ = indexStart;
        currentDrawCommand_.indexCount_ = indexCount;
        currentDrawCommand_.baseVertexIndex_ = 0;
        currentDrawCommand_.instanceStart_ = instanceStart;
        currentDrawCommand_.instanceCount_ = instanceCount;
        drawCommands_.push_back(currentDrawCommand_);
    }

    /// Enqueue draw indexed, instanced geometry with vertex index offset.
    void DrawIndexedInstanced(unsigned indexStart, unsigned indexCount, unsigned baseVertexIndex,
        unsigned instanceStart, unsigned instanceCount)
    {
        assert(currentDrawCommand_.indexBuffer_);
        currentDrawCommand_.indexStart_ = indexStart;
        currentDrawCommand_.indexCount_ = indexCount;
        currentDrawCommand_.baseVertexIndex_ = baseVertexIndex;
        currentDrawCommand_.instanceStart_ = instanceStart;
        currentDrawCommand_.instanceCount_ = instanceCount;
        drawCommands_.push_back(currentDrawCommand_);
    }

    /// Execute commands in the queue.
    void Execute(Graphics* graphics);

private:
    /// Whether to use constant buffers.
    bool useConstantBuffers_{};

    /// Shader parameters data when constant buffers are not used.
    struct ShaderParametersData
    {
        /// Shader parameters collection.
        ShaderParameterCollection collection_;

        /// Current shader parameter group range.
        ShaderParameterRange currentGroupRange_;
    } shaderParameters_;

    /// Shader parameters data when constant buffers are used.
    struct ConstantBuffersData
    {
        /// Constant buffers.
        ConstantBufferCollection collection_;

        /// Current constant buffer layout.
        ConstantBufferLayout* currentLayout_{};
        /// Current pointer to constant buffer data.
        unsigned char* currentData_{};
        /// Current constant buffer layout hashes.
        ea::array<unsigned, MAX_SHADER_PARAMETER_GROUPS> currentHashes_{};
    } constantBuffers_;

    /// Shader resources.
    ShaderResourceCollection shaderResources_;
    /// Draw operations.
    ea::vector<DrawCommandDescription> drawCommands_;

    /// Current draw operation.
    DrawCommandDescription currentDrawCommand_;
    /// Current shader resource group.
    ShaderResourceRange currentShaderResourceGroup_;
};

}
