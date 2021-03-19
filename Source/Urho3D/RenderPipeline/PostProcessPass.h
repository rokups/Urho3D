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

// TODO(renderer): Extract post-process effects to separate file(s)
#include "../RenderPipeline/RenderBuffer.h"
#include "../RenderPipeline/RenderBufferManager.h"

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
    SharedPtr<PipelineState> pipelineState_;

    ea::vector<ShaderParameterDesc> shaderParameters_;
    ea::vector<ShaderResourceDesc> shaderResources_;
};

/// Auto-exposure pos-process.
class URHO3D_API AutoExposurePostProcessPass
    : public PostProcessPass
{
    URHO3D_OBJECT(AutoExposurePostProcessPass, PostProcessPass);

public:
    AutoExposurePostProcessPass(RenderPipelineInterface* renderPipeline, RenderBufferManager* renderBufferManager);

    PostProcessPassFlags GetExecutionFlags() const override { return PostProcessPassFlag::NeedColorOutputReadAndWrite; }
    void Execute() override;

protected:
    bool isFirstFrame_{ true };

    SharedPtr<RenderBuffer> textureHDR128_;
    SharedPtr<RenderBuffer> textureLum64_;
    SharedPtr<RenderBuffer> textureLum16_;
    SharedPtr<RenderBuffer> textureLum4_;
    SharedPtr<RenderBuffer> textureLum1_;
    SharedPtr<RenderBuffer> textureAdaptedLum_;
    SharedPtr<RenderBuffer> texturePrevAdaptedLum_;

    SharedPtr<PipelineState> pipelineStateLum64_;
    SharedPtr<PipelineState> pipelineStateLum16_;
    SharedPtr<PipelineState> pipelineStateLum4_;
    SharedPtr<PipelineState> pipelineStateLum1_;
    SharedPtr<PipelineState> pipelineStateAdaptedLum_;
    SharedPtr<PipelineState> pipelineStateCommitLinear_;
    SharedPtr<PipelineState> pipelineStateCommitGamma_;
};

}
