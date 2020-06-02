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

#include <EASTL/vector.h>
#include <EASTL/fixed_vector.h>

namespace Urho3D
{

/// Thread-safe zero-overhead vector-like container.
template <class T, unsigned N = 16>
class ThreadedVector
{
public:
    /// Max number of threads that require no allocation.
    static const unsigned MaxThreads = N;

    /// Clear collection.
    void Clear(unsigned numThreads)
    {
        elements_.resize(numThreads);
        for (auto& threadCollection : elements_)
            threadCollection.clear();
    }

    /// Insert element into collection.
    void Insert(unsigned threadIndex, const T& value)
    {
        elements_[threadIndex].push_back(value);
    }

    /// Get size.
    unsigned Size() const
    {
        unsigned size = 0;
        for (auto& threadCollection : elements_)
            size += threadCollection.size();
        return size;
    }

    /// Iterate (mutable).
    template <class Callback> void ForEach(const Callback& callback)
    {
        unsigned index = 0;
        for (auto& threadCollection : elements_)
        {
            for (T& element : threadCollection)
            {
                callback(index, element);
                ++index;
            }
        }
    }

    /// Iterate (const).
    template <class Callback> void ForEach(const Callback& callback) const
    {
        unsigned index = 0;
        for (const auto& threadCollection : elements_)
        {
            for (const T& element : threadCollection)
            {
                callback(index, element);
                ++index;
            }
        }
    }

private:
    /// Internal collection.
    ea::fixed_vector<ea::vector<T>, MaxThreads> elements_;
};

}
