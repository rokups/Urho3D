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

#include "../Core/Mutex.h"

#include <EASTL/sort.h>
#include <EASTL/vector.h>

#include <atomic>

namespace Urho3D
{

/// Utility to assign unique non-zero IDs to objects. Thread-safe.
class IndexAllocator
{
public:
    /// Return upper bound of allocated indices.
    unsigned GetNextFreeIndex() const { return nextIndex_.load(std::memory_order_relaxed); }

    /// Allocate index.
    unsigned Allocate()
    {
        MutexLock<Mutex> lock(mutex_);

        unsigned index{};
        if (unusedIndices_.empty())
        {
            index = GetNextFreeIndex();
            nextIndex_.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            index = unusedIndices_.back();
            unusedIndices_.pop_back();
        }

        return index;
    }

    /// Release index. Index should be previously returned from Allocate and not released yet.
    void Release(unsigned index)
    {
        MutexLock<Mutex> lock(mutex_);

        if (index + 1 == GetNextFreeIndex())
            nextIndex_.fetch_sub(1, std::memory_order_relaxed);
        else
            unusedIndices_.push_back(index);
    }

    /// Shrink collection to minimum possible size preserving currently allocated indices.
    void Shrink()
    {
        MutexLock<Mutex> lock(mutex_);

        ea::sort(unusedIndices_.begin(), unusedIndices_.end());
        while (!unusedIndices_.empty() && unusedIndices_.back() + 1 == GetNextFreeIndex())
        {
            unusedIndices_.pop_back();
            nextIndex_.fetch_sub(1, std::memory_order_relaxed);
        }
        unusedIndices_.shrink_to_fit();
    }

private:
    /// Mutex that protects list and index.
    Mutex mutex_;
    /// Next unused index.
    std::atomic_uint32_t nextIndex_{ 1 };
    /// Unused indices.
    ea::vector<unsigned> unusedIndices_;
};

/// Family of unique indices for template type.
template <class T>
class IDFamily
{
public:
    /// Construct.
    IDFamily() { AcquireObjectID(); }

    /// Destruct.
    ~IDFamily() { ReleaseObjectID(); }

    /// Return unique object ID or 0 if not assigned.
    unsigned GetObjectID() const { return objectId_; }

    /// Return upper bound of all used object IDs within family.
    static const unsigned GetNextFreeObjectID() { return indexAllocator.GetNextFreeIndex(); }

    /// Acquire unique object ID. Ignored if unique ID is already acquired.
    void AcquireObjectID()
    {
        if (!objectId_)
            objectId_ = indexAllocator.Allocate();
    }

    /// Release unique object ID. Ignored if unique ID is already released.
    void ReleaseObjectID()
    {
        if (objectId_)
        {
            indexAllocator.Release(objectId_);
            objectId_ = 0;
        }
    }

private:
    /// Shared allocator for this family.
    static IndexAllocator indexAllocator;
    /// Unique object ID.
    unsigned objectId_{};
};

template <class T> IndexAllocator IDFamily<T>::indexAllocator;

}
