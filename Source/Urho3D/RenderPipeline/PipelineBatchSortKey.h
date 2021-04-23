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

#include "../Graphics/Geometry.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Material.h"
#include "../Graphics/Technique.h"
#include "../RenderPipeline/DrawableProcessor.h"
#include "../RenderPipeline/BatchCompositor.h"

#include <EASTL/span.h>

namespace Urho3D
{

/// Scene batch sorted by pipeline state, material and geometry. Also sorted front to back.
struct PipelineBatchByState
{
    /// Primary key layout (from least to most important)
    /// @{
    static constexpr unsigned long long PixelLightBits      = 8;
    static constexpr unsigned long long LightmapBits        = 8;
    static constexpr unsigned long long MaterialBits        = 16;
    static constexpr unsigned long long PipelineStateBits   = 8;
    static constexpr unsigned long long ShaderProgramBits   = 16;
    static constexpr unsigned long long RenderOrderBits     = 8;

    static constexpr unsigned long long PixelLightMask      = (1ull << PixelLightBits) - 1;
    static constexpr unsigned long long LightmapMask        = (1ull << LightmapBits) - 1;
    static constexpr unsigned long long MaterialMask        = (1ull << MaterialBits) - 1;
    static constexpr unsigned long long PipelineStateMask   = (1ull << PipelineStateBits) - 1;
    static constexpr unsigned long long ShaderProgramMask   = (1ull << ShaderProgramBits) - 1;
    static constexpr unsigned long long RenderOrderMask     = (1ull << RenderOrderBits) - 1;

    static constexpr unsigned long long PixelLightOffset    = 0;
    static constexpr unsigned long long LightmapOffset      = PixelLightOffset    + PixelLightBits;
    static constexpr unsigned long long MaterialOffset      = LightmapOffset      + LightmapBits;
    static constexpr unsigned long long PipelineStateOffset = MaterialOffset      + MaterialBits;
    static constexpr unsigned long long ShaderProgramOffset = PipelineStateOffset + PipelineStateBits;
    static constexpr unsigned long long RenderOrderOffset   = ShaderProgramOffset + ShaderProgramBits;

    static_assert(RenderOrderOffset + RenderOrderBits == 64, "Unexpected mask layout");
    static_assert(PixelLightMask | LightmapMask | MaterialMask
        | PipelineStateMask | ShaderProgramMask | RenderOrderMask == 0xffffffffffffffffull, "Unexpected mask layout");
    /// @}

    /// Secondary key layout (from least to most important)
    /// @{
    static constexpr unsigned long long ReservedBits        = 16;
    static constexpr unsigned long long VertexLightsBits    = 24;
    static constexpr unsigned long long GeometryBits        = 24;

    static constexpr unsigned long long ReservedMask        = (1ull << ReservedBits) - 1;
    static constexpr unsigned long long VertexLightsMask    = (1ull << VertexLightsBits) - 1;
    static constexpr unsigned long long GeometryMask        = (1ull << GeometryBits) - 1;

    static constexpr unsigned long long ReservedOffset      = 0;
    static constexpr unsigned long long VertexLightsOffset  = ReservedOffset + ReservedBits;
    static constexpr unsigned long long GeometryOffset      = VertexLightsOffset + VertexLightsBits;

    static_assert(GeometryOffset + GeometryBits == 64, "Unexpected mask layout");
    static_assert(ReservedMask | VertexLightsMask | GeometryMask == 0xffffffffffffffffull, "Unexpected mask layout");
    /// @}

    /// Primary sorting value.
    unsigned long long primaryKey_{};
    /// Secondary sorting value.
    unsigned long long secondaryKey_{};
    /// Batch to be sorted.
    const PipelineBatch* pipelineBatch_{};

    /// Construct default.
    PipelineBatchByState() = default;

    /// Construct from batch.
    explicit PipelineBatchByState(const PipelineBatch* batch)
        : pipelineBatch_(batch)
    {
        // Calculate primary key
        primaryKey_ |= (batch->material_->GetRenderOrder() & RenderOrderMask) << RenderOrderOffset;
        primaryKey_ |= (batch->pipelineState_->GetShaderID() & ShaderProgramMask) << ShaderProgramOffset;
        primaryKey_ |= (batch->pipelineState_->GetObjectID() & PipelineStateMask) << PipelineStateOffset;
        primaryKey_ |= (batch->material_->GetObjectID() & MaterialMask) << MaterialOffset;
        primaryKey_ |= (batch->lightmapIndex_ & LightmapMask) << LightmapOffset;
        primaryKey_ |= (batch->pixelLightIndex_ & PixelLightMask) << PixelLightOffset;

        // Calculate secondary key
        secondaryKey_ |= (batch->geometry_->GetObjectID() & GeometryMask) << GeometryOffset;
        secondaryKey_ |= (batch->vertexLightsHash_ & VertexLightsMask) << VertexLightsOffset;
    }

    /// Compare sorted batches.
    bool operator < (const PipelineBatchByState& rhs) const
    {
        if (primaryKey_ != rhs.primaryKey_)
            return primaryKey_ < rhs.primaryKey_;
        return secondaryKey_ < rhs.secondaryKey_;
    }
};

/// Pipeline batch sorted by render order and back to front.
struct PipelineBatchBackToFront
{
    /// Render order.
    unsigned char renderOrder_{};
    /// Sorting distance.
    float distance_{};
    /// Batch to be sorted.
    const PipelineBatch* pipelineBatch_{};

    /// Construct default.
    PipelineBatchBackToFront() = default;

    /// Construct from batch.
    explicit PipelineBatchBackToFront(const PipelineBatch* batch)
        : renderOrder_(batch->material_->GetRenderOrder())
        , pipelineBatch_(batch)
    {
        distance_ = batch->distance_;
    }

    /// Compare sorted batches.
    bool operator < (const PipelineBatchBackToFront& rhs) const
    {
        if (renderOrder_ != rhs.renderOrder_)
            return renderOrder_ < rhs.renderOrder_;
        return distance_ > rhs.distance_;
    }
};

/// Group of batches to be rendered.
template <class PipelineBatchSorted>
struct PipelineBatchGroup
{
    ea::span<const PipelineBatchSorted> batches_;
    BatchRenderFlags flags_;
    unsigned startInstance_{};
    unsigned numInstances_{};
};

}
