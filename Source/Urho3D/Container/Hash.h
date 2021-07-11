//
// Copyright (c) 2008-2020 the Urho3D project.
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

#include "../Container/Ptr.h"

#include <cstddef>
#include <type_traits>

#include <EASTL/utility.h>
#include <EASTL/weak_ptr.h>
#include <EASTL/vector.h>
#include <EASTL/unordered_map.h>

namespace Urho3D
{

/// Combine hash into result value.
inline void CombineHash(unsigned& result, unsigned hash)
{
    result ^= hash + 0x9e3779b9 + (result << 6) + (result >> 2);
}

/// Fold 64-bit hash to 32-bit.
inline unsigned FoldHash(unsigned long long value)
{
    auto result = static_cast<unsigned>(value);
    CombineHash(result, static_cast<unsigned>(value >> 32ull));
    return result;
}

/// Make hash for floating-point variable with zero error tolerance.
inline unsigned MakeHash(float value)
{
    unsigned uintValue{};
    memcpy(&uintValue, &value, sizeof(float));
    CombineHash(uintValue, 0); // shuffle it a bit
    return uintValue;
}

/// Make hash template helper.
template <class T>
inline unsigned MakeHash(const T& value)
{
    const auto hash = ea::hash<T>{}(value);
    if constexpr (sizeof(hash) > 4)
        return FoldHash(hash);
    else
        return hash;
}

}

namespace eastl
{

template <class T, class Enabled> struct hash;

namespace detail
{
template<typename T, typename = void> struct is_hashable : std::false_type { };
template<typename T> struct is_hashable<T, decltype(void(&T::ToHash))> : std::true_type { };
}

template <typename T>
struct hash<T, typename enable_if<detail::is_hashable<T>::value>::type>
{
    size_t operator()(const T& value) const
    {
        return value.ToHash();
    }
};

template <class T, class U, class Enabled> struct hash<pair<T, U>, Enabled>
{
    size_t operator()(const pair<T, U>& s) const
    {
        return hash<T>()(s.first) ^ hash<U>()(s.second) * 16777619;
    }
};

template <class U>
struct hash<weak_ptr<U>>
{
    size_t operator()(const weak_ptr<U>& value) const
    {
        return (size_t)(void*)value.Get();
    }
};

template <class T, class U> struct hash<pair<T, U>>
{
    size_t operator()(const pair<T, U>& s) const
    {
        return hash<T>()(s.first) ^ hash<U>()(s.second) * 16777619;
    }
};

template <class T, class Allocator> struct hash<vector<T, Allocator>>
{
    size_t operator()(const vector<T, Allocator>& s) const
    {
        size_t result = 16777619;
        for (const T& value : s)
            result = result * 31 + hash<T>()(value);
        return result;
    }
};

template <typename Key, typename T> struct hash<unordered_map<Key, T>>
{
    size_t operator()(const unordered_map<Key, T>& s) const
    {
        size_t result = 16777619;
        for (const auto& pair : s)
            result = result * 31 * 31 + hash<Key>()(pair.first) * 31 + hash<T>()(pair.second);
        return result;
    }
};

}
