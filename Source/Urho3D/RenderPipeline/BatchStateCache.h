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
#include "../RenderPipeline/RenderPipelineDefs.h"

namespace Urho3D
{

class Geometry;
class Material;
class Pass;
class Drawable;
class LightProcessor;
struct SourceBatch;

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
    LightProcessor* pixelLight_{};
    unsigned pixelLightIndex_{};
};

/// Pipeline state cache entry. May be invalid.
struct CachedBatchState
{
    /// Whether the PipelineState is invalidated and should be recreated.
    mutable std::atomic_bool invalidated_{ true };
    SharedPtr<PipelineState> pipelineState_;

    /// Hashes of corresponding objects at the moment of pipeline state creation
    /// @{
    unsigned geometryHash_{};
    unsigned materialHash_{};
    unsigned passHash_{};
    /// @}
};

/// External context that is not present in the key but is necessary to create new pipeline state.
struct BatchStateCreateContext
{
    /// Object that owns BatchStateCache that invoked the callback.
    Object* pass_{};
    /// Index of subpass. Exact meaning depends on actual type of owner pass.
    unsigned subpassIndex_{};
    unsigned shadowSplitIndex_{};
};

/// Pipeline state cache for RenderPipeline batches.
class URHO3D_API BatchStateCache : public NonCopyable
{
public:
    /// Invalidate cache.
    void Invalidate();
    /// Return existing pipeline state or nullptr if not found. Thread-safe.
    /// Resulting state may be invalid.
    PipelineState* GetPipelineState(const BatchStateLookupKey& key) const;
    /// Return existing or create new pipeline state. Not thread safe.
    /// Resulting state may be invalid.
    PipelineState* GetOrCreatePipelineState(const BatchStateCreateKey& key,
        const BatchStateCreateContext& ctx, BatchStateCacheCallback* callback);

private:
    /// Cached states, possibly invalid.
    ea::unordered_map<BatchStateLookupKey, CachedBatchState> cache_;
};

/// Key used to lookup cached pipeline states for UI batches.
/// It's assumed that all UI batches use the same vertex and index buffer formats and material pass.
struct UIBatchStateKey
{
    bool linearOutput_{};
    Material* material_{};
    Pass* pass_{};
    BlendMode blendMode_{};

    bool operator ==(const UIBatchStateKey& rhs) const
    {
        return linearOutput_ == rhs.linearOutput_
            && material_ == rhs.material_
            && pass_ == rhs.pass_
            && blendMode_ == rhs.blendMode_;
    }

    unsigned ToHash() const
    {
        unsigned hash = 0;
        CombineHash(hash, MakeHash(linearOutput_));
        CombineHash(hash, MakeHash(material_));
        CombineHash(hash, MakeHash(pass_));
        CombineHash(hash, MakeHash(blendMode_));
        return hash;
    }
};

/// Pipeline state UI batch cache entry. May be invalid.
struct CachedUIBatchState
{
    /// Whether the PipelineState is invalidated and should be recreated.
    SharedPtr<PipelineState> pipelineState_;
    mutable std::atomic_bool invalidated_{ true };

    /// Hashes of corresponding objects at the moment of pipeline state creation
    /// @{
    unsigned materialHash_{};
    unsigned passHash_{};
    /// @}
};

/// External context that is not present in the key but is necessary to create new pipeline state for UI batch.
struct UIBatchStateCreateContext
{
    VertexBuffer* vertexBuffer_{};
    IndexBuffer* indexBuffer_{};
};

/// Pipeline state cache for UI batches.
class URHO3D_API UIBatchStateCache : public NonCopyable
{
public:
    /// Invalidate cache.
    void Invalidate();
    /// Return existing pipeline state or nullptr if not found. Thread-safe.
    /// Resulting state may be invalid.
    PipelineState* GetPipelineState(const UIBatchStateKey& key) const;
    /// Return existing or create new pipeline state. Not thread safe.
    /// Resulting state may be invalid.
    PipelineState* GetOrCreatePipelineState(const UIBatchStateKey& key,
        const UIBatchStateCreateContext& ctx, UIBatchStateCacheCallback* callback);

private:
    /// Cached states, possibly invalid.
    ea::unordered_map<UIBatchStateKey, CachedUIBatchState> cache_;
};

/// Default implementation of UIBatchStateCache.
class DefaultUIBatchStateCache
    : public Object
    , public UIBatchStateCache
    , public UIBatchStateCacheCallback
{
    URHO3D_OBJECT(DefaultUIBatchStateCache, Object);

public:
    DefaultUIBatchStateCache(Context* context) : Object(context) {}

    PipelineState* GetOrCreatePipelineState(const UIBatchStateKey& key, const UIBatchStateCreateContext& ctx);

private:
    /// Implement UIBatchStateCacheCallback
    /// @{
    SharedPtr<PipelineState> CreateUIBatchPipelineState(const UIBatchStateKey& key, const UIBatchStateCreateContext& ctx) override;
    /// @}

    ea::string vertexShaderDefines_;
    ea::string pixelShaderDefines_;
};

}
