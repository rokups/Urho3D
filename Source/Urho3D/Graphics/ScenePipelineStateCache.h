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

#include "../Graphics/GraphicsDefs.h"
#include "../Graphics/PipelineState.h"
#include "../Graphics/SceneBatch.h"

namespace Urho3D
{

class SceneLight;

/// Key of cached pipeline state cache, unique within viewport.
struct ScenePipelineStateKey
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
    ScenePipelineStateKey() = default;

    /// Construct from base, litbase, light or shadow batch.
    ScenePipelineStateKey(const BaseSceneBatch& sceneBatch, unsigned lightHash)
        : drawableHash_(sceneBatch.drawable_->GetPipelineStateHash())
        , lightHash_(lightHash)
        , geometryType_(sceneBatch.geometryType_)
        , geometry_(sceneBatch.geometry_)
        , material_(sceneBatch.material_)
        , pass_(sceneBatch.pass_)
    {
    }

    /// Compare.
    bool operator ==(const ScenePipelineStateKey& rhs) const
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

/// Pipeline state cache entry. May be invalid.
struct ScenePipelineStateEntry
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
struct ScenePipelineStateContext
{
    /// Cull camera.
    Camera* camera_{};
    /// Scene light.
    SceneLight* light_{};
    /// Drawable.
    Drawable* drawable_{};
};

/// Pipeline state cache callback used to create actual pipeline state.
class ScenePipelineStateCacheCallback
{
public:
    /// Create pipeline state given context and key.
    /// Only attributes that constribute to pipeline state hashes are safe to use.
    virtual SharedPtr<PipelineState> CreatePipelineState(
        const ScenePipelineStateKey& key, const ScenePipelineStateContext& ctx) = 0;
};

/// Pipeline state cache.
class ScenePipelineStateCache
{
public:
    /// Return existing pipeline state. Thread-safe.
    PipelineState* GetPipelineState(const ScenePipelineStateKey& key) const;
    /// Return existing or create new pipeline state. Not thread safe.
    PipelineState* GetOrCreatePipelineState(const ScenePipelineStateKey& key,
        ScenePipelineStateContext& ctx, ScenePipelineStateCacheCallback& callback);

private:
    /// Cached states, possibly invalid.
    ea::unordered_map<ScenePipelineStateKey, ScenePipelineStateEntry> cache_;
};

}
