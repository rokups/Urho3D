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

#include <EASTL/utility.h>

namespace Urho3D
{

/// Helper class to iterate over the range.
template <class T>
struct IteratorRange : public ea::pair<T, T>
{
    /// Construct empty.
    IteratorRange() = default;
    /// Construct valid.
    IteratorRange(const T& begin, const T& end) : ea::pair<T, T>(begin, end) {}
};

/// Make iterator range.
template <class T> IteratorRange<T> MakeIteratorRange(const T& begin, const T& end) { return { begin, end }; }

template <class T> T begin(const IteratorRange<T>& range) { return range.first; }
template <class T> T end(const IteratorRange<T>& range) { return range.second; }

}
