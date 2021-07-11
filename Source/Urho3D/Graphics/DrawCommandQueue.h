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

#pragma once

#include "../Core/Object.h"
#include "../Graphics/ConstantBuffer.h"
#include "../Graphics/ShaderProgramLayout.h"
#include "../Graphics/Geometry.h"
#include "../Graphics/IndexBuffer.h"
#include "../Graphics/PipelineState.h"
#include "../Graphics/ShaderParameterCollection.h"
#include "../Graphics/ConstantBufferCollection.h"
#include "../IO/Log.h"

namespace Urho3D
{

class Graphics;

/// Reference to input shader resource. Only textures are supported now.
struct ShaderResourceDesc
{
    TextureUnit unit_{};
    Texture* texture_{};
};

/// Generic description of shader parameter.
/// Beware of Variant allocations for types larger than Vector4!
struct ShaderParameterDesc
{
    StringHash name_;
    Variant value_;
};

/// Collection of shader resources.
using ShaderResourceCollection = ea::vector<ShaderResourceDesc>;

/// Shader parameter group, range in array. Plain old data.
struct ShaderParameterRange { unsigned first; unsigned second; };

/// Shader resource group, range in array.
using ShaderResourceRange = ea::pair<unsigned, unsigned>;

/// Description of draw command.
struct DrawCommandDescription
{
    PipelineState* pipelineState_{};
    GeometryBufferArray inputBuffers_;

    union
    {
        ea::array<ShaderParameterRange, MAX_SHADER_PARAMETER_GROUPS> shaderParameters_;
        ea::array<ConstantBufferCollectionRef, MAX_SHADER_PARAMETER_GROUPS> constantBuffers_;
    };

    ShaderResourceRange shaderResources_;

    /// Index of scissor rectangle. 0 if disabled.
    unsigned scissorRect_{};

    /// Draw call parameters
    /// @{
    unsigned indexStart_{};
    unsigned indexCount_{};
    unsigned baseVertexIndex_{};
    unsigned instanceStart_{};
    unsigned instanceCount_{};
#ifdef URHO3D_D3D9
    unsigned vertexStart_{};
    unsigned vertexCount_{};
#endif
    /// @}
};

/// Queue of draw commands.
class DrawCommandQueue : public RefCounted
{
public:
    /// Construct.
    DrawCommandQueue(Graphics* graphics);

    /// Reset queue.
    void Reset(bool preferConstantBuffers = true);

    /// Set pipeline state. Must be called first.
    void SetPipelineState(PipelineState* pipelineState)
    {
        assert(pipelineState);
        currentDrawCommand_.pipelineState_ = pipelineState;

        if (useConstantBuffers_)
        {
            constantBuffers_.currentLayout_ = pipelineState->GetShaderProgramLayout();
        }
    }

    /// Set scissor rect.
    void SetScissorRect(const IntRect& scissorRect)
    {
        if (scissorRects_.size() > 1 && scissorRects_.back() == scissorRect)
            return;

        currentDrawCommand_.scissorRect_ = scissorRects_.size();
        scissorRects_.push_back(scissorRect);
    }

    /// Begin shader parameter group. All parameters shall be set for each draw command.
    bool BeginShaderParameterGroup(ShaderParameterGroup group, bool differentFromPrevious = false)
    {
        if (useConstantBuffers_)
        {
            const unsigned groupLayoutHash = constantBuffers_.currentLayout_->GetConstantBufferHash(group);

            // If constant buffer for this group is currently disabled...
            if (groupLayoutHash == 0)
            {
                // If contents changed, forget cached constant buffer
                if (differentFromPrevious)
                    constantBuffers_.currentHashes_[group] = 0;
                return false;
            }

            // If data and/or layout changed, rebuild block
            if (differentFromPrevious || groupLayoutHash != constantBuffers_.currentHashes_[group])
            {
                const unsigned size = constantBuffers_.currentLayout_->GetConstantBufferSize(group);
                const auto& refAndData = constantBuffers_.collection_.AddBlock(size);

                currentDrawCommand_.constantBuffers_[group] = refAndData.first;
                constantBuffers_.currentData_ = refAndData.second;
                constantBuffers_.currentHashes_[group] = groupLayoutHash;
                constantBuffers_.currentGroup_ = group;
                return true;
            }

            return false;
        }
        else
        {
            // Allocate new group if different from previous or group is not initialized yet
            const ShaderParameterRange& groupRange = currentDrawCommand_.shaderParameters_[group];
            const bool groupInitialized = groupRange.first != groupRange.second;
            return differentFromPrevious || !groupInitialized;
        }
    }

    /// Add shader parameter. Shall be called only if BeginShaderParameterGroup returned true.
    template <class T>
    void AddShaderParameter(StringHash name, const T& value)
    {
        if (useConstantBuffers_)
        {
            const auto& paramInfo = constantBuffers_.currentLayout_->GetConstantBufferParameter(name);
            if (paramInfo.offset_ != M_MAX_UNSIGNED)
            {
                if (constantBuffers_.currentGroup_ != paramInfo.group_)
                {
                    URHO3D_LOGERROR("Shader parameter #{} '{}' shall be stored in group {} instead of group {}",
                        name.Value(), name.Reverse(), constantBuffers_.currentGroup_, paramInfo.group_);
                    return;
                }

                if (!ConstantBufferCollection::StoreParameter(constantBuffers_.currentData_ + paramInfo.offset_,
                    paramInfo.size_, value))
                {
                    URHO3D_LOGERROR("Shader parameter #{} '{}' has unexpected type, {} bytes expected",
                        name.Value(), name.Reverse(), paramInfo.size_);
                }
            }
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
            constantBuffers_.currentGroup_ = MAX_SHADER_PARAMETER_GROUPS;
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
        shaderResources_.push_back(ShaderResourceDesc{ unit, texture });
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
    void SetBuffers(const GeometryBufferArray& buffers)
    {
        currentDrawCommand_.inputBuffers_ = buffers;
    }

    /// Enqueue draw non-indexed geometry.
    void Draw(unsigned vertexStart, unsigned vertexCount)
    {
        currentDrawCommand_.inputBuffers_.indexBuffer_ = nullptr;
        currentDrawCommand_.indexStart_ = vertexStart;
        currentDrawCommand_.indexCount_ = vertexCount;
        currentDrawCommand_.baseVertexIndex_ = 0;
        currentDrawCommand_.instanceStart_ = 0;
        currentDrawCommand_.instanceCount_ = 0;
#ifdef URHO3D_D3D9
        currentDrawCommand_.vertexStart_ = vertexStart;
        currentDrawCommand_.vertexCount_ = vertexCount;
#endif
        drawCommands_.push_back(currentDrawCommand_);
    }

    /// Enqueue draw indexed geometry.
    void DrawIndexed(unsigned indexStart, unsigned indexCount)
    {
        assert(currentDrawCommand_.inputBuffers_.indexBuffer_);
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
        assert(currentDrawCommand_.inputBuffers_.indexBuffer_);
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
        assert(currentDrawCommand_.inputBuffers_.indexBuffer_);
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
        assert(currentDrawCommand_.inputBuffers_.indexBuffer_);
        currentDrawCommand_.indexStart_ = indexStart;
        currentDrawCommand_.indexCount_ = indexCount;
        currentDrawCommand_.baseVertexIndex_ = baseVertexIndex;
        currentDrawCommand_.instanceStart_ = instanceStart;
        currentDrawCommand_.instanceCount_ = instanceCount;
        drawCommands_.push_back(currentDrawCommand_);
    }

    /// Enqueue draw indexed geometry (compatible with D3D9).
    void DrawIndexedLegacy(unsigned indexStart, unsigned indexCount,
        unsigned vertexStart, unsigned vertexCount)
    {
        assert(currentDrawCommand_.inputBuffers_.indexBuffer_);
        currentDrawCommand_.indexStart_ = indexStart;
        currentDrawCommand_.indexCount_ = indexCount;
        currentDrawCommand_.baseVertexIndex_ = 0;
        currentDrawCommand_.instanceStart_ = 0;
        currentDrawCommand_.instanceCount_ = 0;
#ifdef URHO3D_D3D9
        currentDrawCommand_.vertexStart_ = vertexStart;
        currentDrawCommand_.vertexCount_ = vertexCount;
#endif
        drawCommands_.push_back(currentDrawCommand_);
    }

    /// Enqueue draw indexed geometry with vertex index offset (compatible with D3D9).
    void DrawIndexedLegacy(unsigned indexStart, unsigned indexCount, unsigned baseVertexIndex,
        unsigned vertexStart, unsigned vertexCount)
    {
        assert(currentDrawCommand_.inputBuffers_.indexBuffer_);
        currentDrawCommand_.indexStart_ = indexStart;
        currentDrawCommand_.indexCount_ = indexCount;
        currentDrawCommand_.baseVertexIndex_ = baseVertexIndex;
        currentDrawCommand_.instanceStart_ = 0;
        currentDrawCommand_.instanceCount_ = 0;
#ifdef URHO3D_D3D9
        currentDrawCommand_.vertexStart_ = vertexStart;
        currentDrawCommand_.vertexCount_ = vertexCount;
#endif
        drawCommands_.push_back(currentDrawCommand_);
    }

    /// Enqueue draw indexed, instanced geometry (compatible with D3D9).
    void DrawIndexedInstancedLegacy(unsigned indexStart, unsigned indexCount, unsigned instanceStart,
        unsigned vertexStart, unsigned vertexCount, unsigned instanceCount)
    {
        assert(currentDrawCommand_.inputBuffers_.indexBuffer_);
        currentDrawCommand_.indexStart_ = indexStart;
        currentDrawCommand_.indexCount_ = indexCount;
        currentDrawCommand_.baseVertexIndex_ = 0;
        currentDrawCommand_.instanceStart_ = instanceStart;
        currentDrawCommand_.instanceCount_ = instanceCount;
#ifdef URHO3D_D3D9
        currentDrawCommand_.vertexStart_ = vertexStart;
        currentDrawCommand_.vertexCount_ = vertexCount;
#endif
        drawCommands_.push_back(currentDrawCommand_);
    }

    /// Enqueue draw indexed, instanced geometry with vertex index offset (compatible with D3D9).
    void DrawIndexedInstancedLegacy(unsigned indexStart, unsigned indexCount, unsigned baseVertexIndex,
        unsigned vertexStart, unsigned vertexCount, unsigned instanceStart, unsigned instanceCount)
    {
        assert(currentDrawCommand_.inputBuffers_.indexBuffer_);
        currentDrawCommand_.indexStart_ = indexStart;
        currentDrawCommand_.indexCount_ = indexCount;
        currentDrawCommand_.baseVertexIndex_ = baseVertexIndex;
        currentDrawCommand_.instanceStart_ = instanceStart;
        currentDrawCommand_.instanceCount_ = instanceCount;
#ifdef URHO3D_D3D9
        currentDrawCommand_.vertexStart_ = vertexStart;
        currentDrawCommand_.vertexCount_ = vertexCount;
#endif
        drawCommands_.push_back(currentDrawCommand_);
    }

    /// Execute commands in the queue.
    void Execute();

private:
    /// Cached pointer to Graphics.
    Graphics* graphics_{};
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

        /// Current constant buffer group.
        ShaderParameterGroup currentGroup_{ MAX_SHADER_PARAMETER_GROUPS };
        /// Current constant buffer layout.
        ShaderProgramLayout* currentLayout_{};
        /// Current pointer to constant buffer data.
        unsigned char* currentData_{};
        /// Current constant buffer layout hashes.
        ea::array<unsigned, MAX_SHADER_PARAMETER_GROUPS> currentHashes_{};
    } constantBuffers_;

    /// Shader resources.
    ShaderResourceCollection shaderResources_;
    /// Scissor rects.
    ea::vector<IntRect> scissorRects_;
    /// Draw operations.
    ea::vector<DrawCommandDescription> drawCommands_;

    /// Current draw operation.
    DrawCommandDescription currentDrawCommand_;
    /// Current shader resource group.
    ShaderResourceRange currentShaderResourceGroup_;
};

}
