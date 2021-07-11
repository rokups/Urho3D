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

#include "../Core/NonCopyable.h"
#include "../Graphics/GraphicsDefs.h"
#include "../Graphics/PipelineState.h"
//#include "../RenderPipeline/PipelineBatchSortKey.h"

namespace Urho3D
{

class Geometry;
class Material;
class Pass;
class Drawable;
class LightProcessor;

/// Key used to lookup cached pipeline states for PipelineBatch.
///
/// PipelineState creation may depend only on variables that contribute to BatchStateLookupKey:
///
/// - Parameters of Drawable that contribute to hash calculation. Key does not depend on Drawable for better reuse.
/// - Parameters of per-pixel Light that contribute to hash calculation (for both lit and shadow geometry rendering).
/// - Geometry type from SourceBatch.
/// - Hashed state of Geometry.
/// - Hashed state of Material.
/// - Hashed state of Pass.
struct BatchStateLookupKey
{
    unsigned drawableHash_{};
    unsigned pixelLightHash_{};
    GeometryType geometryType_{};
    Geometry* geometry_{};
    Material* material_{};
    Pass* pass_{};

    bool operator ==(const BatchStateLookupKey& rhs) const
    {
        return drawableHash_ == rhs.drawableHash_
            && pixelLightHash_ == rhs.pixelLightHash_
            && geometryType_ == rhs.geometryType_
            && geometry_ == rhs.geometry_
            && material_ == rhs.material_
            && pass_ == rhs.pass_;
    }

    unsigned ToHash() const
    {
        unsigned hash = 0;
        CombineHash(hash, MakeHash(drawableHash_));
        CombineHash(hash, MakeHash(pixelLightHash_));
        CombineHash(hash, MakeHash(geometryType_));
        CombineHash(hash, MakeHash(geometry_));
        CombineHash(hash, MakeHash(material_));
        CombineHash(hash, MakeHash(pass_));
        return hash;
    }
};

/// Key used to create cached pipeline states for PipelineBatch.
/// Contains actual objects instead of just hashes.
struct BatchStateCreateKey : public BatchStateLookupKey
{
    Drawable* drawable_{};
    unsigned sourceBatchIndex_{};
    LightProcessor* pixelLight_{};
    unsigned pixelLightIndex_{};
    unsigned vertexLightsHash_{};
};

/// Pipeline state cache entry. May be invalid.
struct CachedBatchState
{
    /// Hashes of corresponding objects at the moment of caching
    /// @{
    unsigned geometryHash_{};
    unsigned materialHash_{};
    unsigned passHash_{};
    /// @}

    SharedPtr<PipelineState> pipelineState_;
    /// Whether the PipelineState is invalidated and should be recreated.
    mutable std::atomic_bool invalidated_{ true };
};

/// External context that is not present in the key but is necessary to create new pipeline state.
struct BatchStateCreateContext
{
    /// Pointer to the pass.
    Object* pass_{};
    /// Index of subpass.
    unsigned subpassIndex_{};
};

/// Pipeline state cache callback used to create actual pipeline state.
class BatchStateCacheCallback
{
public:
    /// Create pipeline state given context and key.
    /// Only attributes that constribute to pipeline state hashes are safe to use.
    virtual SharedPtr<PipelineState> CreateBatchPipelineState(
        const BatchStateCreateKey& key, const BatchStateCreateContext& ctx) = 0;
};

/// Pipeline state cache for RenderPipeline batches.
class URHO3D_API BatchStateCache : public NonCopyable
{
public:
    /// Invalidate cache.
    void Invalidate();
    /// Return existing pipeline state or nullptr if not found. Thread-safe.
    /// Resulting state is always valid.
    PipelineState* GetPipelineState(const BatchStateLookupKey& key) const;
    /// Return existing or create new pipeline state. Not thread safe.
    /// Resulting state may be invalid.
    PipelineState* GetOrCreatePipelineState(const BatchStateCreateKey& key,
        BatchStateCreateContext& ctx, BatchStateCacheCallback* callback);

private:
    /// Cached states, possibly invalid.
    ea::unordered_map<BatchStateLookupKey, CachedBatchState> cache_;
};

}
