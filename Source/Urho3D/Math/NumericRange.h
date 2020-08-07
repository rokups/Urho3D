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

#include <EASTL/algorithm.h>
#include <EASTL/numeric_limits.h>
#include <EASTL/utility.h>

namespace Urho3D
{

/// Numerical range (pair of min and max value). Invalid if flipped.
template <class T>
struct NumericRange : ea::pair<T, T>
{
    /// Construct invalid.
    NumericRange() : ea::pair<T, T>(ea::numeric_limits<T>::max(), ea::numeric_limits<T>::lowest()) {};

    /// Construct valid.
    NumericRange(const T& minValue, const T& maxValue) : ea::pair<T, T>(minValue, maxValue) { }

    /// Return whether the range is valid.
    bool IsValid() const { return this->first <= this->second; }

    /// Return whether the range intersects another.
    bool Interset(const NumericRange& rhs) const
    {
        return this->first <= rhs.second && rhs.first <= this->second;
    }

    /// Accumulate range.
    NumericRange<T>& operator |= (const NumericRange& rhs)
    {
        this->first = ea::min(this->first, rhs.first);
        this->second = ea::max(this->second, rhs.second);
        return *this;
    }

    /// Accumulate range.
    NumericRange<T> operator | (const NumericRange& rhs) const
    {
        auto lhs = *this;
        lhs |= rhs;
        return lhs;
    }

    /// Trim range.
    NumericRange<T>& operator &= (const NumericRange& rhs)
    {
        this->first = ea::max(this->first, rhs.first);
        this->second = ea::min(this->second, rhs.second);
        return *this;
    }

    /// Trim range.
    NumericRange<T> operator & (const NumericRange& rhs) const
    {
        auto lhs = *this;
        lhs &= rhs;
        return lhs;
    }
};

}
