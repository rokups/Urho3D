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

#include "../Container/IndexAllocator.h"
#include "../Container/Hash.h"
#include "../Container/RefCounted.h"
#include "../Graphics/GraphicsDefs.h"
#include "../Graphics/GPUObject.h"
#include "../Graphics/IndexBuffer.h"
#include "../Graphics/ShaderProgramLayout.h"
#include "../Graphics/VertexBuffer.h"

#include <EASTL/algorithm.h>
#include <EASTL/array.h>
#include <EASTL/span.h>

namespace Urho3D
{

class Geometry;
class PipelineStateCache;
class ShaderVariation;

/// Set of input buffers with vertex and index data.
struct GeometryBufferArray
{
    IndexBuffer* indexBuffer_{};
    ea::array<VertexBuffer*, MAX_VERTEX_STREAMS> vertexBuffers_{};

    GeometryBufferArray() = default;

    GeometryBufferArray(std::initializer_list<VertexBuffer*> vertexBuffers,
        IndexBuffer* indexBuffer, VertexBuffer* instancingBuffer)
    {
        ea::span<VertexBuffer* const> tempVertexBuffers(vertexBuffers.begin(), vertexBuffers.size());
        Initialize(tempVertexBuffers, indexBuffer, instancingBuffer);
    }

    template <class Container>
    GeometryBufferArray(Container&& vertexBuffers, IndexBuffer* indexBuffer, VertexBuffer* instancingBuffer)
    {
        Initialize(vertexBuffers, indexBuffer, instancingBuffer);
    }

    explicit GeometryBufferArray(Geometry* geometry, VertexBuffer* instancingBuffer = nullptr);

private:
    template <class Container>
    void Initialize(Container&& vertexBuffers, IndexBuffer* indexBuffer, VertexBuffer* instancingBuffer)
    {
        using namespace ea;

        const unsigned numVertexBuffers = size(vertexBuffers);
        assert(numVertexBuffers + !!instancingBuffer <= MAX_VERTEX_STREAMS);

        const auto iter = ea::copy(begin(vertexBuffers), end(vertexBuffers), vertexBuffers_.begin());
        if (instancingBuffer)
            *iter = instancingBuffer;

        indexBuffer_ = indexBuffer;
    }
};

/// Description structure used to create PipelineState.
/// Should contain all relevant information about input layout,
/// shader resources and parameters and pipeline configuration.
/// PipelineState is automatically updated on shader reload.
/// TODO: Store render target formats here as well
struct PipelineStateDesc
{
    static const unsigned MaxNumVertexElements = 32;

    PrimitiveType primitiveType_{};

    /// Input layout
    /// @{
    unsigned numVertexElements_{};
    ea::array<VertexElement, MaxNumVertexElements> vertexElements_;
    IndexBufferType indexType_{};

    void InitializeInputLayout(const GeometryBufferArray& buffers);
    void InitializeInputLayoutAndPrimitiveType(Geometry* geometry, VertexBuffer* instancingBuffer = nullptr);
    /// @}

    /// Shaders
    /// @{
    ShaderVariation* vertexShader_{};
    ShaderVariation* pixelShader_{};
    /// @}

    /// Depth-stencil state
    /// @{
    bool depthWriteEnabled_{};
    bool stencilTestEnabled_{};
    CompareMode depthCompareFunction_{};
    CompareMode stencilCompareFunction_{};
    StencilOp stencilOperationOnPassed_{};
    StencilOp stencilOperationOnStencilFailed_{};
    StencilOp stencilOperationOnDepthFailed_{};
    unsigned stencilReferenceValue_{};
    unsigned stencilCompareMask_{};
    unsigned stencilWriteMask_{};
    /// @}

    /// Rasterizer state
    /// @{
    FillMode fillMode_{};
    CullMode cullMode_{};
    float constantDepthBias_{};
    float slopeScaledDepthBias_{};
    bool scissorTestEnabled_{};
    bool lineAntiAlias_{};
    /// @}

    /// Blend state
    /// @{
    bool colorWriteEnabled_{};
    BlendMode blendMode_{};
    bool alphaToCoverageEnabled_{};
    /// @}

    /// Cached hash of the structure.
    unsigned hash_{};
    unsigned ToHash() const { return hash_; }

    bool operator ==(const PipelineStateDesc& rhs) const
    {
        if (hash_ != rhs.hash_)
            return false;

        return primitiveType_ == rhs.primitiveType_

            && numVertexElements_ == rhs.numVertexElements_
            && vertexElements_ == rhs.vertexElements_
            && indexType_ == rhs.indexType_

            && vertexShader_ == rhs.vertexShader_
            && pixelShader_ == rhs.pixelShader_

            && depthWriteEnabled_ == rhs.depthWriteEnabled_
            && stencilTestEnabled_ == rhs.stencilTestEnabled_
            && depthCompareFunction_ == rhs.depthCompareFunction_
            && stencilCompareFunction_ == rhs.stencilCompareFunction_
            && stencilOperationOnPassed_ == rhs.stencilOperationOnPassed_
            && stencilOperationOnStencilFailed_ == rhs.stencilOperationOnStencilFailed_
            && stencilOperationOnDepthFailed_ == rhs.stencilOperationOnDepthFailed_
            && stencilReferenceValue_ == rhs.stencilReferenceValue_
            && stencilCompareMask_ == rhs.stencilCompareMask_
            && stencilWriteMask_ == rhs.stencilWriteMask_

            && fillMode_ == rhs.fillMode_
            && cullMode_ == rhs.cullMode_
            && constantDepthBias_ == rhs.constantDepthBias_
            && slopeScaledDepthBias_ == rhs.slopeScaledDepthBias_
            && scissorTestEnabled_ == rhs.scissorTestEnabled_
            && lineAntiAlias_ == rhs.lineAntiAlias_

            && colorWriteEnabled_ == rhs.colorWriteEnabled_
            && blendMode_ == rhs.blendMode_
            && alphaToCoverageEnabled_ == rhs.alphaToCoverageEnabled_;
    }

    /// Return whether the description structure is properly initialized.
    bool IsInitialized() const
    {
        return vertexShader_ && pixelShader_;
    }

    void RecalculateHash()
    {
        unsigned hash = 0;
        CombineHash(hash, primitiveType_);

        CombineHash(hash, numVertexElements_);
        for (unsigned i = 0; i < numVertexElements_; ++i)
            CombineHash(hash, vertexElements_[i].ToHash());
        CombineHash(hash, indexType_);

        CombineHash(hash, MakeHash(vertexShader_));
        CombineHash(hash, MakeHash(pixelShader_));

        CombineHash(hash, depthWriteEnabled_);
        CombineHash(hash, depthCompareFunction_);
        CombineHash(hash, stencilTestEnabled_);
        CombineHash(hash, stencilCompareFunction_);
        CombineHash(hash, stencilOperationOnPassed_);
        CombineHash(hash, stencilOperationOnStencilFailed_);
        CombineHash(hash, stencilOperationOnDepthFailed_);
        CombineHash(hash, stencilReferenceValue_);
        CombineHash(hash, stencilCompareMask_);
        CombineHash(hash, stencilWriteMask_);

        CombineHash(hash, fillMode_);
        CombineHash(hash, cullMode_);
        CombineHash(hash, MakeHash(constantDepthBias_));
        CombineHash(hash, MakeHash(slopeScaledDepthBias_));
        CombineHash(hash, scissorTestEnabled_);
        CombineHash(hash, lineAntiAlias_);

        CombineHash(hash, colorWriteEnabled_);
        CombineHash(hash, blendMode_);
        CombineHash(hash, alphaToCoverageEnabled_);

        // Consider 0-hash invalid
        hash_ = ea::max(1u, hash);
    }
};

/// Cooked pipeline state. It's not an Object to keep it lightweight.
class URHO3D_API PipelineState : public RefCounted, public IDFamily<PipelineState>
{
public:
    explicit PipelineState(PipelineStateCache* owner);
    ~PipelineState() override;
    void Setup(const PipelineStateDesc& desc);
    void ResetCachedState();
    void RestoreCachedState(Graphics* graphics);

    /// Set pipeline state to GPU.
    void Apply(Graphics* graphics);

    /// Getters
    /// @{
    bool IsValid() const { return !!shaderProgramLayout_; }
    const PipelineStateDesc& GetDesc() const { return desc_; }
    ShaderProgramLayout* GetShaderProgramLayout() const { return shaderProgramLayout_; }
    unsigned GetShaderID() const { return shaderProgramLayout_->GetObjectID(); }
    /// @}

private:
    WeakPtr<PipelineStateCache> owner_;
    PipelineStateDesc desc_;
    WeakPtr<ShaderProgramLayout> shaderProgramLayout_{};
};

/// Generic pipeline state cache.
class URHO3D_API PipelineStateCache : public Object, private GPUObject
{
    URHO3D_OBJECT(PipelineStateCache, Object);

public:
    explicit PipelineStateCache(Context* context);

    /// Create new or return existing pipeline state. Returned state may be invalid.
    /// Return nullptr if description is malformed.
    SharedPtr<PipelineState> GetPipelineState(PipelineStateDesc desc);

    /// Internal. Remove pipeline state with given description from cache.
    void ReleasePipelineState(const PipelineStateDesc& desc);

private:
    /// GPUObject callbacks
    /// @{
    void OnDeviceLost() override;
    void OnDeviceReset() override;
    void Release() override;
    /// @}

    void HandleResourceReload(StringHash eventType, VariantMap& eventData);

    ea::unordered_map<PipelineStateDesc, WeakPtr<PipelineState>> states_;
};

}
