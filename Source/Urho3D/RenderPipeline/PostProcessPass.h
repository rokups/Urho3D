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

#include "../Container/FlagSet.h"
#include "../Core/Object.h"
#include "../Graphics/DrawCommandQueue.h"
#include "../RenderPipeline/RenderPipelineDefs.h"

#include <EASTL/vector.h>

namespace Urho3D
{

class PipelineState;
class RenderBufferManager;
class RenderPipelineInterface;

/// Post-processing pass of render pipeline. Expected to output to color buffer.
class URHO3D_API PostProcessPass
    : public Object
{
    URHO3D_OBJECT(PostProcessPass, Object);

public:
    PostProcessPass(RenderPipelineInterface* renderPipeline, RenderBufferManager* renderBufferManager);
    ~PostProcessPass() override;

    virtual PostProcessPassFlags GetExecutionFlags() const = 0;
    virtual void Execute() = 0;

protected:
    RenderBufferManager* renderBufferManager_{};
};

/// Base class for simplest post-process effects.
class URHO3D_API SimplePostProcessPass
    : public PostProcessPass
{
    URHO3D_OBJECT(SimplePostProcessPass, PostProcessPass);

public:
    SimplePostProcessPass(RenderPipelineInterface* renderPipeline, RenderBufferManager* renderBufferManager,
        PostProcessPassFlags flags, BlendMode blendMode,
        const ea::string& shaderName, const ea::string& shaderDefines);

    void AddShaderParameter(StringHash name, const Variant& value);
    void AddShaderResource(TextureUnit unit, Texture* texture);

    PostProcessPassFlags GetExecutionFlags() const override { return flags_; }
    void Execute() override;

protected:
    const PostProcessPassFlags flags_;
    const ea::string debugComment_;
    SharedPtr<PipelineState> pipelineState_;

    ea::vector<ShaderParameterDesc> shaderParameters_;
    ea::vector<ShaderResourceDesc> shaderResources_;
};

}
