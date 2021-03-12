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

#include <EASTL/unordered_map.h>

#include "../../Math/MathDefs.h"
#include "../../Graphics/ShaderVariation.h"

namespace Urho3D
{

/// Combined information for specific vertex and pixel shaders.
class ShaderProgram : public ShaderProgramLayout
{
public:
    /// Construct.
    ShaderProgram(ShaderVariation* vertexShader, ShaderVariation* pixelShader)
    {
        const ea::unordered_map<StringHash, ShaderParameter>& vsParams = vertexShader->GetParameters();
        for (auto i = vsParams.begin(); i != vsParams.end(); ++i)
            parameters_[i->first] = i->second;

        const ea::unordered_map<StringHash, ShaderParameter>& psParams = pixelShader->GetParameters();
        for (auto i = psParams.begin(); i != psParams.end(); ++i)
            parameters_[i->first] = i->second;

        // Optimize shader parameter lookup by rehashing to next power of two
        parameters_.rehash(Max(2, NextPowerOfTwo(parameters_.size())));
    }

    /// Combined parameters from the vertex and pixel shader.
    ea::unordered_map<StringHash, ShaderParameter> parameters_;
};

}
