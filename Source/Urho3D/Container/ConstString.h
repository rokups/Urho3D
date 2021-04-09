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

#include "../Math/StringHash.h"

#include <EASTL/string.h>

namespace Urho3D
{

/// Immutable string with precomputed hash.
/// Inherit it from string to allow implicit casts to ea::string w/o allocation.
/// It could be done with cast operator too, but this code would cause crash if `operator const ea::string&()` is used:
/// `const ea::string& s2 = ConstString("text");`
class ConstString : public ea::string
{
public:
    /// Construct
    /// @{
    ConstString() = default;
    ConstString(ea::string_view str)
        : ea::string(str)
        , hash_(c_str())
    {
    }
    /// @}

    /// Return hash
    /// @{
    StringHash GetHash() const { return hash_; }
    operator StringHash() const { return hash_; }
    /// @}

private:
    const StringHash hash_;
};

}
