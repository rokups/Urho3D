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

#include "../Graphics/ShaderProgramLayout.h"

namespace Urho3D
{

void ShaderProgramLayout::AddConstantBuffer(ShaderParameterGroup group, unsigned size)
{
    constantBufferSizes_[group] = size;
}

void ShaderProgramLayout::AddConstantBufferParameter(StringHash name, ShaderParameterGroup group, unsigned offset, unsigned size)
{
    constantBufferParameters_.emplace(name, ConstantBufferElement{ group, offset, size });
}

void ShaderProgramLayout::RecalculateLayoutHash()
{
    for (unsigned i = 0; i < MAX_SHADER_PARAMETER_GROUPS; ++i)
    {
        constantBufferHashes_[i] = 0;
        if (constantBufferSizes_[i] != 0)
            CombineHash(constantBufferHashes_[i], constantBufferSizes_[i]);
    }

    for (const auto& item : constantBufferParameters_)
    {
        const StringHash paramName = item.first;
        const ConstantBufferElement& element = item.second;
        CombineHash(constantBufferHashes_[element.group_], paramName.Value());
        CombineHash(constantBufferHashes_[element.group_], element.offset_);
        CombineHash(constantBufferHashes_[element.group_], element.size_);

        if (constantBufferHashes_[element.group_] == 0)
            constantBufferHashes_[element.group_] = 1;
    }
}

}
