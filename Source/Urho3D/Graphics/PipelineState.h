//
// Copyright (c) 2008-2020 the Urho3D project.
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

#include "../Container/Hash.h"
#include "../Container/RefCounted.h"
#include "../Graphics/GraphicsDefs.h"
#include "../Graphics/VertexBuffer.h"

#include <EASTL/algorithm.h>
#include <EASTL/vector.h>

namespace Urho3D
{

class ShaderVariation;

/// Type of index buffer.
enum IndexBufferType
{
    IBT_NONE = 0,
    IBT_UINT16,
    IBT_UINT32
};

/// Pipeline state description.
struct PipelineStateDesc
{
    /// Input layout: vertex elements.
    ea::vector<VertexElement> vertexElements_;

    /// Vertex shader used.
    ShaderVariation* vertexShader_{};
    /// Pixel shader used.
    ShaderVariation* pixelShader_{};

    /// Primitive type.
    PrimitiveType primitiveType_{};
    /// Index type.
    IndexBufferType indexType_{};

    /// Whether the depth write enabled.
    bool depthWrite_{};
    /// Depth compare function.
    CompareMode depthMode_{};
    /// Whether the stencil enabled.
    bool stencilEnabled_{};
    /// Stencil compare function.
    CompareMode stencilMode_{};
    /// Stencil pass op.
    StencilOp stencilPass_{};
    /// Stencil fail op.
    StencilOp stencilFail_{};
    /// Stencil depth test fail op.
    StencilOp stencilDepthFail_{};
    /// Stencil reference value.
    unsigned stencilRef_{};
    /// Stencil compare mask.
    unsigned compareMask_{};
    /// Stencil write mask.
    unsigned writeMask_{};

    /// Whether the color write enabled.
    bool colorWrite_{};
    /// Stencil write mask.
    BlendMode blendMode_{};
    /// Whether the a2c is enabled.
    bool alphaToCoverage_{};

    /// Fill mode.
    FillMode fillMode_{};
    /// Cull mode.
    CullMode cullMode_{};
    /// Constant depth bias.
    float constantDepthBias_{};
    /// Slope-scaled depth bias.
    float slopeScaledDepthBias_{};

    /// Cached hash of the structure.
    unsigned hash_{};
    /// Return hash.
    unsigned ToHash() const { return hash_; }

    /// Compare.
    bool operator ==(const PipelineStateDesc& rhs) const
    {
        if (hash_ != rhs.hash_)
            return false;

        return vertexElements_ == rhs.vertexElements_

            && vertexShader_ == rhs.vertexShader_
            && pixelShader_ == rhs.pixelShader_

            && primitiveType_ == rhs.primitiveType_
            && indexType_ == rhs.indexType_

            && depthWrite_ == rhs.depthWrite_
            && depthMode_ == rhs.depthMode_
            && stencilEnabled_ == rhs.stencilEnabled_
            && stencilMode_ == rhs.stencilMode_
            && stencilPass_ == rhs.stencilPass_
            && stencilFail_ == rhs.stencilFail_
            && stencilDepthFail_ == rhs.stencilDepthFail_
            && stencilRef_ == rhs.stencilRef_
            && compareMask_ == rhs.compareMask_
            && writeMask_ == rhs.writeMask_

            && colorWrite_ == rhs.colorWrite_
            && blendMode_ == rhs.blendMode_
            && alphaToCoverage_ == rhs.alphaToCoverage_

            && fillMode_ == rhs.fillMode_
            && cullMode_ == rhs.cullMode_
            && constantDepthBias_ == rhs.constantDepthBias_
            && slopeScaledDepthBias_ == rhs.slopeScaledDepthBias_;
    }

    /// Recalculate hash.
    void RecalculateHash()
    {
        unsigned hash = 0;
        CombineHash(hash, vertexElements_.size());
        for (const VertexElement& element : vertexElements_)
        {
            CombineHash(hash, element.type_);
            CombineHash(hash, element.semantic_);
            CombineHash(hash, element.index_);
            CombineHash(hash, element.perInstance_);
            CombineHash(hash, element.offset_);
        }

        CombineHash(hash, MakeHash(vertexShader_));
        CombineHash(hash, MakeHash(pixelShader_));

        CombineHash(hash, primitiveType_);
        CombineHash(hash, indexType_);

        CombineHash(hash, depthWrite_);
        CombineHash(hash, depthMode_);
        CombineHash(hash, stencilEnabled_);
        CombineHash(hash, stencilMode_);
        CombineHash(hash, stencilPass_);
        CombineHash(hash, stencilFail_);
        CombineHash(hash, stencilDepthFail_);
        CombineHash(hash, stencilRef_);
        CombineHash(hash, compareMask_);
        CombineHash(hash, writeMask_);

        CombineHash(hash, colorWrite_);
        CombineHash(hash, blendMode_);
        CombineHash(hash, alphaToCoverage_);

        CombineHash(hash, fillMode_);
        CombineHash(hash, cullMode_);
        CombineHash(hash, constantDepthBias_);
        CombineHash(hash, slopeScaledDepthBias_);

        // Consider 0-hash invalid
        if (hash == 0)
            hash = 1;
    }
};

/// Pipeline state container.
class PipelineState : public Object
{
    URHO3D_OBJECT(PipelineState, Object);

public:
    /// Construct.
    explicit PipelineState(Context* context) : Object(context) {}
    /// Create cached state.
    void Create(const PipelineStateDesc& desc) { desc_ = desc; }

    /// Return description.
    const PipelineStateDesc& GetDesc() const { return desc_; }

private:
    /// Description.
    PipelineStateDesc desc_;
};

}
