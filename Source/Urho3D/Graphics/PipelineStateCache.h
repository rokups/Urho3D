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

#include "../Graphics/GraphicsDefs.h"
#include "../Graphics/VertexBuffer.h"

#include <EASTL/algorithm.h>
#include <EASTL/vector.h>

namespace Urho3D
{

class VertexShader;
class PixelShader;

/// Pipeline state description.
struct PipelineStateDesc
{
    /// Input layout: hash of vertex elements for all buffers.
    unsigned vertexElementsHash_{};
    /// Input layout: vertex elements.
    ea::vector<VertexElement> vertexElements_;

    /// Vertex shader used.
    VertexShader* vertexShader_{};
    /// Pixel shader used.
    PixelShader* pixelShader_{};

    /// Primitive type.
    PrimitiveType primitiveType_{};
    /// Whether the large indices are used.
    bool largeIndices_{};

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
    /// Recalculate hash.
};

class PipelineState
{

};

class PipelineStateCache
{

};

}
