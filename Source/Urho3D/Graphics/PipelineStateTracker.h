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

#include <atomic>

namespace Urho3D
{

/// Helper class to track pipeline state changes caused by derived class.
class PipelineStateTracker
{
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
    /// Mark pipeline state hash as dirty.
    void MarkPipelineStateHashDirty() { pipelineStateHash_.store(0, std::memory_order_relaxed); }

private:
    /// Recalculate hash. Shall be save to call from multiple threads as long as the object is not changing.
    virtual unsigned RecalculatePipelineStateHash() const = 0;
    /// Cached hash.
    mutable std::atomic_uint32_t pipelineStateHash_;
};

}
