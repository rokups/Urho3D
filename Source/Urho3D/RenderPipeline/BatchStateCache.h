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
//#include "../RenderPipeline/SceneBatch.h"

namespace Urho3D
{

class Geometry;
class Material;
class Pass;
class Drawable;
class LightProcessor;
//class BatchCompositorPass;

/// Key used to lookup cached pipeline states for PipelineBatch.
///
/// PipelineState creation may depend only on variables that contribute to BatchStateLookupKey:
///
/// - Parameters of Drawable that contribute to hash calculation. Key does not depend on Drawable for better reuse.
/// - Parameters of relevant Light or LightProcessor that contribute to hash calculation. Relevant for forward rendering only.
/// - Geometry type from SourceBatch.
/// - Hashed state of Geometry.
/// - Hashed state of Material.
/// - Hashed state of Pass.
struct BatchStateLookupKey
{
    /// Drawable settings that affect pipeline state.
    unsigned drawableHash_{};
    /// Light settings that affect pipeline state.
    unsigned lightHash_{};
    /// Geometry type.
    GeometryType geometryType_{};
    /// Geometry to be rendered.
    Geometry* geometry_{};
    /// Material to be rendered.
    Material* material_{};
    /// Pass of the material technique to be used.
    Pass* pass_{};

    /// Construct default.
    /*BatchStateLookupKey() = default;

    /// Construct from base, litbase, light or shadow batch.
    // TODO(renderer): Remove
    BatchStateLookupKey(const PipelineBatch& sceneBatch, unsigned lightHash)
        : drawableHash_(sceneBatch.drawable_->GetPipelineStateHash())
        , lightHash_(lightHash)
        , geometryType_(sceneBatch.geometryType_)
        , geometry_(sceneBatch.geometry_)
        , material_(sceneBatch.material_)
        , pass_(sceneBatch.pass_)
    {
    }*/

    /// Compare.
    bool operator ==(const BatchStateLookupKey& rhs) const
    {
        return drawableHash_ == rhs.drawableHash_
            && lightHash_ == rhs.lightHash_
            && geometryType_ == rhs.geometryType_
            && geometry_ == rhs.geometry_
            && material_ == rhs.material_
            && pass_ == rhs.pass_;
    }

    /// Return hash.
    unsigned ToHash() const
    {
        unsigned hash = 0;
        CombineHash(hash, MakeHash(drawableHash_));
        CombineHash(hash, MakeHash(lightHash_));
        CombineHash(hash, MakeHash(geometryType_));
        CombineHash(hash, MakeHash(geometry_));
        CombineHash(hash, MakeHash(material_));
        CombineHash(hash, MakeHash(pass_));
        return hash;
    }
};

/// Key used to create cached pipeline states for PipelineBatch.
/// Contains actual objects instead of hashes.
struct BatchStateCreateKey : public BatchStateLookupKey
{
    /// Drawable geometry that requested this state.
    Drawable* drawable_{};
    /// Index of source batch within geometry.
    unsigned sourceBatchIndex_{};
    /// Light processor corresponging to per-pixel forward light.
    LightProcessor* light_{};
    /// Index of per-pixel forward light.
    unsigned lightIndex_{};
};


/// Pipeline state cache entry. May be invalid.
struct CachedBatchState
{
    /// Cached state of the geometry.
    unsigned geometryHash_{};
    /// Cached state of the material.
    unsigned materialHash_{};
    /// Cached state of the pass.
    unsigned passHash_{};

    /// Pipeline state.
    SharedPtr<PipelineState> pipelineState_;
    /// Whether the state is invalidated.
    mutable std::atomic_bool invalidated_;
};

/// External context that is not present in the key but is necessary to create new pipeline state.
struct BatchStateCreateContext
{
    /// Pointer to the pass.
    Object* pass_{};
    /// Index of subpass.
    unsigned subpassIndex_{};
    /// Pass shader defines.
    //ea::string shaderDefines_;
    ///
    //bool litBasePass_{};
    /// Whether it is shadow pass.
    //bool shadowPass_{};
    /// Cull camera.
    //Camera* camera_{};
    /// Scene light.
    //const LightProcessor* light_{};
    /// Drawable.
    //Drawable* drawable_{};
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
    /// Return existing pipeline state. Thread-safe.
    PipelineState* GetPipelineState(const BatchStateLookupKey& key) const;
    /// Return existing or create new pipeline state. Not thread safe.
    PipelineState* GetOrCreatePipelineState(const BatchStateCreateKey& key,
        BatchStateCreateContext& ctx, BatchStateCacheCallback& callback);

private:
    /// Cached states, possibly invalid.
    ea::unordered_map<BatchStateLookupKey, CachedBatchState> cache_;
};

// TODO(renderer): Remove me
using ScenePipelineStateKey = BatchStateLookupKey;
using ScenePipelineStateCache = BatchStateCache;
using ScenePipelineStateCacheCallback = BatchStateCacheCallback;

}
