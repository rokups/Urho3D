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

#include "../Urho3D.h"
#include "../Container/Ptr.h"

#include <atomic>
#include <EASTL/vector.h>
#include <EASTL/algorithm.h>

namespace Urho3D
{

class PipelineStateTracker;

/// Dependency from one PipelineStateTracker to another. Neither checks nor affects objects lifetime.
class URHO3D_API PipelineStateDependency
{
public:
    /// Construct default.
    PipelineStateDependency() = default;

    /// Construct valid.
    PipelineStateDependency(PipelineStateTracker* dependency, PipelineStateTracker* dependant) { Reset(dependency, dependant); }

    /// Copy-construct.
    PipelineStateDependency(const PipelineStateDependency& other) { Reset(other.dependency_, other.dependant_); }

    /// Move-construct.
    PipelineStateDependency(PipelineStateDependency&& other)
    {
        ea::swap(dependency_, other.dependency_);
        ea::swap(dependant_, other.dependant_);
    }

    /// Destruct.
    ~PipelineStateDependency() { Reset(nullptr, nullptr); }

    /// Assign.
    PipelineStateDependency& operator =(const PipelineStateDependency& other)
    {
        Reset(other.dependency_, other.dependant_);
        return *this;
    }

    /// Move-assign.
    PipelineStateDependency& operator =(PipelineStateDependency&& other)
    {
        ea::swap(dependency_, other.dependency_);
        ea::swap(dependant_, other.dependant_);
        return *this;
    }

private:
    /// Reset and update dependency.
    void Reset(PipelineStateTracker* dependency, PipelineStateTracker* dependant);

    /// Owned pointer.
    PipelineStateTracker* dependency_{};
    /// Owner.
    PipelineStateTracker* dependant_{};
};

/// Helper class to track pipeline state changes caused by derived class.
class URHO3D_API PipelineStateTracker
{
    friend class PipelineStateDependency;

public:
    /// Return (partial) pipeline state hash. Save to call from multiple threads as long as the object is not changing.
    unsigned GetPipelineStateHash() const
    {
        const unsigned hash = pipelineStateHash_.load(std::memory_order_relaxed);
        if (hash)
            return hash;

        const unsigned newHash = ea::max(1u, RecalculatePipelineStateHash());
        pipelineStateHash_.store(newHash, std::memory_order_relaxed);
        return newHash;
    }

protected:
    /// Create dependency onto another pipeline state.
    PipelineStateDependency CreateDependency(PipelineStateTracker* dependency);
    /// Add reference to dependant pipeline state tracker.
    void AddObserverReference(PipelineStateTracker* dependant);
    /// Remove reference to dependant pipeline state tracker.
    void RemoveObserverReference(PipelineStateTracker* dependant);
    /// Mark pipeline state hash as dirty.
    void MarkPipelineStateHashDirty();

private:
    /// Vector of dependants with reference counters.
    using DependantVector = ea::vector<ea::pair<PipelineStateTracker*, unsigned>>;
    /// Recalculate hash (must not be non zero). Shall be save to call from multiple threads as long as the object is not changing.
    virtual unsigned RecalculatePipelineStateHash() const = 0;
    /// Find dependant by pointer.
    DependantVector::iterator FindDependantTrackerIter(PipelineStateTracker* dependant);

    /// Cached hash.
    mutable std::atomic_uint32_t pipelineStateHash_;
    /// Other pipeline state trackers depending on this tracker.
    DependantVector dependantTrackers_;
};

}
