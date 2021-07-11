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

#include "../RenderPipeline/SceneProcessor.h"
#include "../RenderPipeline/CameraProcessor.h"
#include "../RenderPipeline/RenderBuffer.h"
#include "../RenderPipeline/RenderBufferManager.h"
#include "../RenderPipeline/RenderPipeline.h"
#include "../RenderPipeline/PostProcessPass.h"
#include "../RenderPipeline/ScenePass.h"

#include <EASTL/optional.h>

namespace Urho3D
{

class ShadowMapAllocator;

/// Default implementation of render pipeline instance.
class URHO3D_API DefaultRenderPipelineView
    : public RenderPipelineView
{
    URHO3D_OBJECT(DefaultRenderPipelineView, RenderPipelineView);

public:
    explicit DefaultRenderPipelineView(RenderPipeline* renderPipeline);
    ~DefaultRenderPipelineView() override;

    const RenderPipelineSettings& GetSettings() const { return settings_; }
    void SetSettings(const RenderPipelineSettings& settings);

    /// Implement RenderPipelineInterface
    /// @{
    RenderPipelineDebugger* GetDebugger() override { return &debugger_; }
    /// @}

    /// Implement RenderPipelineView
    /// @{
    bool Define(RenderSurface* renderTarget, Viewport* viewport);
    void Update(const FrameInfo& frameInfo);
    void Render();
    const FrameInfo& GetFrameInfo() const override;
    const RenderPipelineStats& GetStats() const override { return stats_; }
    /// @}

protected:
    unsigned RecalculatePipelineStateHash() const;
    void SendViewEvent(StringHash eventType);
    void ApplySettings();

private:
    RenderPipelineSettings settings_;
    unsigned settingsPipelineStateHash_{};
    bool settingsDirty_{};

    /// Previous pipeline state hash.
    unsigned oldPipelineStateHash_{};

    CommonFrameInfo frameInfo_;
    PostProcessPassFlags postProcessFlags_;

    RenderPipelineStats stats_;
    RenderPipelineDebugger debugger_;

    SharedPtr<RenderBufferManager> renderBufferManager_;
    SharedPtr<ShadowMapAllocator> shadowMapAllocator_;
    SharedPtr<InstancingBuffer> instancingBuffer_;
    SharedPtr<SceneProcessor> sceneProcessor_;

    SharedPtr<UnorderedScenePass> depthPrePass_;
    SharedPtr<UnorderedScenePass> opaquePass_;
    SharedPtr<UnorderedScenePass> postOpaquePass_;
    SharedPtr<BackToFrontScenePass> alphaPass_;
    SharedPtr<BackToFrontScenePass> postAlphaPass_;

    struct DeferredLightingData
    {
        SharedPtr<RenderBuffer> albedoBuffer_;
        SharedPtr<RenderBuffer> specularBuffer_;
        SharedPtr<RenderBuffer> normalBuffer_;
    };
    ea::optional<DeferredLightingData> deferred_;

    ea::vector<SharedPtr<PostProcessPass>> postProcessPasses_;
};

}
