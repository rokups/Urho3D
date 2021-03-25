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

#include "../Core/Object.h"
#include "../Core/Signal.h"
#include "../Graphics/Drawable.h"
#include "../RenderPipeline/SceneProcessor.h"
#include "../RenderPipeline/CameraProcessor.h"
#include "../RenderPipeline/RenderBuffer.h"
#include "../RenderPipeline/RenderBufferManager.h"
#include "../RenderPipeline/RenderPipelineDefs.h"
#include "../RenderPipeline/PostProcessPass.h"
#include "../RenderPipeline/ScenePass.h"
#include "../Scene/Serializable.h"

#include <EASTL/optional.h>

namespace Urho3D
{

class RenderPipeline;
class RenderSurface;
class Viewport;
class ShadowMapAllocator;

enum class PostProcessAntialiasing
{
    None,
    FXAA2,
    FXAA3
};

struct RenderPipelineSettings
{
    RenderBufferManagerSettings renderBufferManager_;
    SceneProcessorSettings sceneProcessor_;
    ShadowMapAllocatorSettings shadowMapAllocator_;
    InstancingBufferSettings instancingBuffer_;

    ToneMappingPassSettings toneMapping_;
    PostProcessAntialiasing antialiasing_{};
    bool greyScale_{};
};

///
class URHO3D_API RenderPipelineView
    : public Object
    , public RenderPipelineInterface
{
    URHO3D_OBJECT(RenderPipelineView, Object);

public:
    explicit RenderPipelineView(RenderPipeline* renderPipeline);
    ~RenderPipelineView() override;

    /// Implement RenderPipelineInterface
    /// @{
    Context* GetContext() const override { return BaseClassName::GetContext(); }
    /// @}

    RenderPipeline* GetRenderPipeline() const { return renderPipeline_; }
    const RenderPipelineSettings& GetSettings() const { return settings_; }
    void SetSettings(const RenderPipelineSettings& settings);

    /// Rendering
    /// @{
    bool Define(RenderSurface* renderTarget, Viewport* viewport);
    void Update(const FrameInfo& frameInfo);
    void Render();
    /// @}

protected:
    unsigned RecalculatePipelineStateHash() const;

    void ValidateSettings();
    void ApplySettings();

private:
    RenderPipeline* const renderPipeline_{};
    Graphics* const graphics_{};
    Renderer* const renderer_{};

    RenderPipelineSettings settings_;
    unsigned settingsPipelineStateHash_{};
    bool settingsDirty_{};

    /// Previous pipeline state hash.
    unsigned oldPipelineStateHash_{};

    CommonFrameInfo frameInfo_;
    PostProcessPassFlags postProcessFlags_;

    SharedPtr<RenderBufferManager> renderBufferManager_;
    SharedPtr<ShadowMapAllocator> shadowMapAllocator_;
    SharedPtr<InstancingBuffer> instancingBuffer_;
    SharedPtr<SceneProcessor> sceneProcessor_;

    SharedPtr<UnorderedScenePass> opaquePass_;
    SharedPtr<BackToFrontScenePass> alphaPass_;
    SharedPtr<UnorderedScenePass> postOpaquePass_;

    struct DeferredLightingData
    {
        SharedPtr<RenderBuffer> albedoBuffer_;
        SharedPtr<RenderBuffer> specularBuffer_;
        SharedPtr<RenderBuffer> normalBuffer_;
    };
    ea::optional<DeferredLightingData> deferred_;

    ea::vector<SharedPtr<PostProcessPass>> postProcessPasses_;
};

///
class URHO3D_API RenderPipeline
    : public Component
{
    URHO3D_OBJECT(RenderPipeline, Component);

public:
    RenderPipeline(Context* context);
    ~RenderPipeline() override;

    static void RegisterObject(Context* context);

    /// Properties
    /// @{
    const RenderPipelineSettings& GetSettings() const { return settings_; }
    void SetSettings(const RenderPipelineSettings& settings);
    /// @}

    /// Create new instance of render pipeline.
    SharedPtr<RenderPipelineView> Instantiate();

    /// Invoked when settings change.
    Signal<void(const RenderPipelineSettings&)> OnSettingsChanged;

private:
    void MarkSettingsDirty();

    RenderPipelineSettings settings_;
};

}
