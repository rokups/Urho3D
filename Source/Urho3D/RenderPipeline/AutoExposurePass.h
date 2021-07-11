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

/// Post-processing pass that adjusts HDR scene exposure.
class URHO3D_API AutoExposurePass
    : public PostProcessPass
{
    URHO3D_OBJECT(AutoExposurePass, PostProcessPass);

public:
    AutoExposurePass(RenderPipelineInterface* renderPipeline, RenderBufferManager* renderBufferManager);
    void SetSettings(const AutoExposurePassSettings& settings);

    PostProcessPassFlags GetExecutionFlags() const override { return PostProcessPassFlag::NeedColorOutputReadAndWrite; }
    void Execute() override;

protected:
    void InitializeTextures();
    void InitializeStates();

    void EvaluateDownsampledColorBuffer();
    void EvaluateLuminance();
    void EvaluateAdaptedLuminance();

    bool isAdaptedLuminanceInitialized_{};
    AutoExposurePassSettings settings_;

    struct CachedTextures
    {
        SharedPtr<RenderBuffer> color128_;
        SharedPtr<RenderBuffer> lum64_;
        SharedPtr<RenderBuffer> lum16_;
        SharedPtr<RenderBuffer> lum4_;
        SharedPtr<RenderBuffer> lum1_;
        SharedPtr<RenderBuffer> adaptedLum_;
        SharedPtr<RenderBuffer> prevAdaptedLum_;
    } textures_;

    struct CachedStates
    {
        SharedPtr<PipelineState> lum64_;
        SharedPtr<PipelineState> lum16_;
        SharedPtr<PipelineState> lum4_;
        SharedPtr<PipelineState> lum1_;
        SharedPtr<PipelineState> adaptedLum_;
        SharedPtr<PipelineState> autoExposure_;

        bool IsValid()
        {
            return lum64_->IsValid()
                && lum16_->IsValid()
                && lum4_->IsValid()
                && lum1_->IsValid()
                && adaptedLum_->IsValid()
                && autoExposure_->IsValid();
        }
    };
    ea::optional<CachedStates> pipelineStates_;
};

}
