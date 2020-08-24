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

#include "../Container/RefCounted.h"
#include "../Graphics/GraphicsDefs.h"

#include <EASTL/unordered_map.h>

namespace Urho3D
{

/// Element of constant buffer.
struct ConstantBufferElement
{
    /// Shader parameter group aka constant buffer index.
    ShaderParameterGroup group_{};
    /// Offset of the element within buffer.
    unsigned offset_{};
    /// Size of element after alignment.
    unsigned stride_{};
    /// Size of array (if applicable) or 1 (if not an array).
    unsigned count_{};
};

/// Description of constant buffer layout of shader program.
class URHO3D_API ConstantBufferLayout : public RefCounted
{
public:
    /// Return constant buffer size for given group.
    unsigned GetConstantBufferSize(ShaderParameterGroup group) const { return constantBufferSizes_[group]; }

    /// Return constant buffer hash for given group.
    unsigned GetConstantBufferHash(ShaderParameterGroup group) const { return constantBufferHashes_[group]; }

    /// Return parameter info by hash.
    const ConstantBufferElement& GetConstantBufferParameter(StringHash name) const
    {
        static const ConstantBufferElement empty{ MAX_SHADER_PARAMETER_GROUPS, M_MAX_UNSIGNED, 0, 0 };
        const auto iter = constantBufferParameters_.find(name);
        if (iter == constantBufferParameters_.end())
            return empty;
        return iter->second;
    }

protected:
    /// Add constant buffer.
    void AddConstantBuffer(ShaderParameterGroup group, unsigned size)
    {
        constantBufferSizes_[group] = size;
    }

    /// Add parameter inside constant buffer.
    void AddConstantBufferParameter(StringHash name, ShaderParameterGroup group,
        unsigned offset, unsigned stride, unsigned count)
    {
        constantBufferParameters_.emplace(name, ConstantBufferElement{ group, offset, stride, count });
    }

    /// Recalculate layout hash.
    void RecalculateLayoutHash()
    {
        for (unsigned i = 0; i < MAX_SHADER_PARAMETER_GROUPS; ++i)
        {
            constantBufferHashes_[i] = 0;
            CombineHash(constantBufferHashes_[i], constantBufferSizes_[i]);
        }

        for (const auto& item : constantBufferParameters_)
        {
            const StringHash paramName = item.first;
            const ConstantBufferElement& element = item.second;
            CombineHash(constantBufferHashes_[element.group_], paramName.Value());
            CombineHash(constantBufferHashes_[element.group_], element.offset_);
            CombineHash(constantBufferHashes_[element.group_], element.stride_);
            CombineHash(constantBufferHashes_[element.group_], element.count_);

            if (constantBufferHashes_[element.group_] == 0)
                constantBufferHashes_[element.group_] = 1;
        }
    }

private:
    /// Constant buffer sizes.
    unsigned constantBufferSizes_[MAX_SHADER_PARAMETER_GROUPS]{};
    /// Constant buffer hashes.
    unsigned constantBufferHashes_[MAX_SHADER_PARAMETER_GROUPS]{};
    /// Mapping from parameter name to (buffer, offset) pair.
    ea::unordered_map<StringHash, ConstantBufferElement> constantBufferParameters_;
};

}
