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

#include "../Graphics/PipelineState.h"
#include "../RenderPipeline/PostProcessPass.h"
#include "../RenderPipeline/RenderBuffer.h"
#include "../RenderPipeline/RenderPipelineDefs.h"

#include <EASTL/optional.h>

namespace Urho3D
{

class RenderBufferManager;
class RenderPipelineInterface;

URHO3D_SHADER_CONST(Bloom, LuminanceWeights);
URHO3D_SHADER_CONST(Bloom, Threshold);
URHO3D_SHADER_CONST(Bloom, InputInvSize);
URHO3D_SHADER_CONST(Bloom, Intensity);

/// Post-processing pass that applies bloom to scene.
class URHO3D_API BloomPass
    : public PostProcessPass
{
    URHO3D_OBJECT(BloomPass, PostProcessPass);

public:
    BloomPass(RenderPipelineInterface* renderPipeline, RenderBufferManager* renderBufferManager);
    void SetSettings(const BloomPassSettings& settings);

    PostProcessPassFlags GetExecutionFlags() const override { return PostProcessPassFlag::NeedColorOutputReadAndWrite | PostProcessPassFlag::NeedColorOutputBilinear; }
    void Execute() override;

protected:
    void InitializeTextures();
    void InitializeStates();

    unsigned GatherBrightRegions(RenderBuffer* destination);
    void BlurTexture(RenderBuffer* final, RenderBuffer* temporary);
    void CopyTexture(RenderBuffer* source, RenderBuffer* destination);
    void ApplyBloom(RenderBuffer* bloom, float intensity);

    auto GetShaderParameters(const Vector2& inputInvSize) const
    {
        const float thresholdGap = ea::max(0.01f, settings_.thresholdMax_ - settings_.threshold_);
        const ShaderParameterDesc result[] = {
            { Bloom_LuminanceWeights, luminanceWeights_ },
            { Bloom_Threshold, Vector2(settings_.threshold_, 1.0f / thresholdGap) },
            { Bloom_InputInvSize, inputInvSize },
        };
        return ea::to_array(result);
    }

    BloomPassSettings settings_;

    struct CachedTextures
    {
        SharedPtr<RenderBuffer> final_;
        SharedPtr<RenderBuffer> temporary_;
    };
    ea::vector<CachedTextures> textures_;

    struct CachedStates
    {
        SharedPtr<PipelineState> bright_;
        SharedPtr<PipelineState> blurV_;
        SharedPtr<PipelineState> blurH_;
        SharedPtr<PipelineState> bloom_;

        bool IsValid()
        {
            return bright_->IsValid()
                && blurV_->IsValid()
                && blurH_->IsValid()
                && bloom_->IsValid();
        }
    };
    ea::optional<CachedStates> pipelineStates_;

    Vector3 luminanceWeights_;
    ea::vector<float> intensityMultipliers_;
};

}
