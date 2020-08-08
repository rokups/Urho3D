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

#include "../Precompiled.h"

#include "../Graphics/Geometry.h"
#include "../Graphics/ScenePipelineStateCache.h"
#include "../Graphics/Technique.h"

#include "../DebugNew.h"

namespace Urho3D
{

PipelineState* ScenePipelineStateCache::GetPipelineState(const ScenePipelineStateKey& key) const
{
    const auto iter = cache_.find(key);
    if (iter == cache_.end() || iter->second.invalidated_.load(std::memory_order_relaxed))
        return nullptr;

    const ScenePipelineStateEntry& entry = iter->second;
    if (key.geometry_->GetPipelineStateHash() != entry.geometryHash_
        || key.material_->GetPipelineStateHash() != entry.materialHash_
        || key.pass_->GetPipelineStateHash() != entry.passHash_)
    {
        entry.invalidated_.store(true, std::memory_order_relaxed);
        return nullptr;
    }

    return entry.pipelineState_;
}

PipelineState* ScenePipelineStateCache::GetOrCreatePipelineState(const ScenePipelineStateKey& key,
    ScenePipelineStateContext& ctx, ScenePipelineStateCacheCallback& callback)
{
    ScenePipelineStateEntry& entry = cache_[key];
    if (!entry.pipelineState_ || entry.invalidated_.load(std::memory_order_relaxed)
        || key.geometry_->GetPipelineStateHash() != entry.geometryHash_
        || key.material_->GetPipelineStateHash() != entry.materialHash_
        || key.pass_->GetPipelineStateHash() != entry.passHash_)
    {
        entry.pipelineState_ = callback.CreatePipelineState(key, ctx);
        entry.geometryHash_ = key.geometry_->GetPipelineStateHash();
        entry.materialHash_ = key.material_->GetPipelineStateHash();
        entry.passHash_ = key.pass_->GetPipelineStateHash();
        entry.invalidated_.store(false, std::memory_order_relaxed);
    }

    return entry.pipelineState_;
}

}
