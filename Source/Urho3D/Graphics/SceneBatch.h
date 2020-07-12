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

#include "../Graphics/Graphics.h"
#include "../Graphics/Material.h"
#include "../Graphics/Technique.h"

namespace Urho3D
{

/// Base or lit base scene batch for specific sub-pass.
struct BaseSceneBatch
{
    /// Drawable index.
    unsigned drawableIndex_{};
    /// Source batch index.
    unsigned sourceBatchIndex_{};
    /// Geometry type used.
    GeometryType geometryType_{};
    /// Drawable to be rendered.
    Drawable* drawable_{};
    /// Geometry to be rendered.
    Geometry* geometry_{};
    /// Material to be rendered.
    Material* material_{};
    /// Material pass to be rendered.
    Pass* pass_{};
    /// Pipeline state.
    PipelineState* pipelineState_{};

    /// Return source batch.
    const SourceBatch& GetSourceBatch() const { return drawable_->GetBatches()[sourceBatchIndex_]; }
};

/// Additional light scene batch for specific sub-pass.
struct LightSceneBatch : public BaseSceneBatch
{
    /// Light.
    Light* light_{};
};

/// Scene batch sorted by pipeline state, material and geometry. Also sorted front to back.
struct BaseSceneBatchSortedByState
{
    /// Sorting value for pipeline state.
    unsigned long long pipelineStateKey_{};
    /// Sorting value for material and geometry.
    unsigned long long materialGeometryKey_{};
    /// Sorting distance.
    float distance_{};
    /// Base, litbase or light batch to be sorted.
    const BaseSceneBatch* sceneBatch_{};

    /// Construct default.
    BaseSceneBatchSortedByState() = default;

    /// Construct from batch.
    explicit BaseSceneBatchSortedByState(const BaseSceneBatch* batch)
        : sceneBatch_(batch)
    {
        const SourceBatch& sourceBatch = batch->GetSourceBatch();

        // 8: render order
        // 32: shader variation
        // 24: pipeline state
        const unsigned long long renderOrder = batch->material_->GetRenderOrder();
        const unsigned long long shaderHash = batch->pipelineState_->GetShaderHash();
        const unsigned pipelineStateHash = MakeHash(batch->pipelineState_);
        pipelineStateKey_ |= renderOrder << 56;
        pipelineStateKey_ |= shaderHash << 24;
        pipelineStateKey_ |= (pipelineStateHash & 0xffffff) ^ (pipelineStateHash >> 24);

        // 32: material
        // 32: geometry
        unsigned long long materialHash = MakeHash(batch->material_);
        materialGeometryKey_ |= (materialHash ^ sourceBatch.lightmapIndex_) << 32;
        materialGeometryKey_ |= MakeHash(batch->geometry_);

        distance_ = sourceBatch.distance_;
    }

    /// Compare sorted batches.
    bool operator < (const BaseSceneBatchSortedByState& rhs) const
    {
        if (pipelineStateKey_ != rhs.pipelineStateKey_)
            return pipelineStateKey_ < rhs.pipelineStateKey_;
        if (materialGeometryKey_ != rhs.materialGeometryKey_)
            return materialGeometryKey_ < rhs.materialGeometryKey_;
        return distance_ > rhs.distance_;
    }
};

/// Scene batch sorted by render order and back to front.
struct BaseSceneBatchSortedBackToFront
{
    /// Render order.
    unsigned char renderOrder_{};
    /// Sorting distance.
    float distance_{};
    /// Batch to be sorted.
    const BaseSceneBatch* sceneBatch_{};

    /// Construct default.
    BaseSceneBatchSortedBackToFront() = default;

    /// Construct from batch.
    explicit BaseSceneBatchSortedBackToFront(const BaseSceneBatch* batch)
        : renderOrder_(batch->material_->GetRenderOrder())
        , sceneBatch_(batch)
    {
        const SourceBatch& sourceBatch = batch->GetSourceBatch();
        distance_ = sourceBatch.distance_;
    }

    /// Compare sorted batches.
    bool operator < (const BaseSceneBatchSortedBackToFront& rhs) const
    {
        if (renderOrder_ != rhs.renderOrder_)
            return renderOrder_ < rhs.renderOrder_;
        return distance_ < rhs.distance_;
    }
};

/// Light batch sorted by light, pipeline state, material and geometry.
struct LightBatchSortedByState : public BaseSceneBatchSortedByState
{
    /// Light.
    Light* light_{};

    /// Construct default.
    LightBatchSortedByState() = default;

    /// Construct from batch.
    explicit LightBatchSortedByState(const LightSceneBatch* lightBatch)
        : BaseSceneBatchSortedByState(lightBatch)
        , light_(lightBatch->light_)
    {
    }

    /// Compare sorted batches.
    bool operator < (const LightBatchSortedByState& rhs) const
    {
        if (light_ != rhs.light_)
            return light_ < rhs.light_;
        if (pipelineStateKey_ != rhs.pipelineStateKey_)
            return pipelineStateKey_ < rhs.pipelineStateKey_;
        return materialGeometryKey_ < rhs.materialGeometryKey_;
    }
};

}
