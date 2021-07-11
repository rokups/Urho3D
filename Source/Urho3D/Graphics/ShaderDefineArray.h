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

/// \file

#pragma once

#include <Urho3D/Urho3D.h>

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/utility.h>

namespace Urho3D
{

/// Array of shader defines: pairs of name and value.
struct URHO3D_API ShaderDefineArray
{
    /// Construct default.
    ShaderDefineArray() = default;

    /// Construct from string.
    explicit ShaderDefineArray(const ea::string& defineString)
    {
        const ea::vector<ea::string> items = defineString.split(' ');
        defines_.reserve(items.size());
        for (const ea::string& item : items)
        {
            const unsigned equalsPos = item.find('=');
            if (equalsPos != ea::string::npos)
                defines_.emplace_back(item.substr(0, equalsPos), item.substr(equalsPos + 1));
            else
                defines_.emplace_back(item, "1");
        }
    }

    /// Append define without value.
    void Append(const ea::string& define) { defines_.emplace_back(define, "1"); }

    /// Append define with value.
    void Append(const ea::string& define, const ea::string& value) { defines_.emplace_back(define, value); }

    /// Return size.
    unsigned Size() const { return defines_.size(); }

    /// Find defines unused in source code.
    ea::vector<ea::string> FindUnused(const ea::string& code) const
    {
        ea::vector<ea::string> unusedDefines;
        for (const auto& define : defines_)
        {
            if (code.find(define.first) == ea::string::npos)
                unusedDefines.push_back(define.first);
        }
        return unusedDefines;
    }

    /// Vector of defines
    ea::vector<ea::pair<ea::string, ea::string>> defines_;
};

/// Return begin iterator of ShaderDefineArray.
inline auto begin(const ShaderDefineArray& defines) { return defines.defines_.begin(); }

/// Return end iterator of ShaderDefineArray.
inline auto end(const ShaderDefineArray& defines) { return defines.defines_.end(); }

}
