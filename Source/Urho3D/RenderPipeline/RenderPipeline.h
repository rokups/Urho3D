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

class Camera;
class RenderPath;
class RenderSurface;
class Scene;
class XMLFile;
class View;
class Viewport;
class ShadowMapAllocator;
class DrawableProcessor;
class SceneBatchCollector;
class BatchRenderer;

enum class PostProcessAntialiasing
{
    None,
    FXAA2,
    FXAA3
};

enum class PostProcessTonemapping
{
    None,
    ReinhardEq3,
    ReinhardEq4,
    Uncharted2,
};

struct PostProcessSettings
{
    PostProcessAntialiasing antialiasing_{};
    PostProcessTonemapping tonemapping_{};
    bool greyScale_{};
};

///
struct RenderPipelineSettings : public BaseRenderPipelineSettings
{
    PostProcessSettings postProcess_;
};

///
class URHO3D_API RenderPipeline
    : public PipelineStateTracker
    , public RenderPipelineInterface
{
    URHO3D_OBJECT(RenderPipeline, RenderPipelineInterface);

public:
    /// Construct with defaults.
    RenderPipeline(Context* context);
    /// Destruct.
    ~RenderPipeline() override;

    /// Register object with the engine.
    static void RegisterObject(Context* context);
    /// Apply attribute changes that can not be applied immediately. Called after scene load or a network update.
    void ApplyAttributes() override;

    /// Define all dependencies.
    bool Define(RenderSurface* renderTarget, Viewport* viewport);

    /// Update.
    void Update(const FrameInfo& frameInfo);

    /// Render.
    void Render();

protected:
    /// Recalculate hash (must not be non zero). Shall be save to call from multiple threads as long as the object is not changing.
    unsigned RecalculatePipelineStateHash() const override;

    void MarkSettingsDirty() { settingsDirty_ = true; }
    void ValidateSettings();
    void ApplySettings();

private:
    Graphics* graphics_{};
    Renderer* renderer_{};
    WorkQueue* workQueue_{};

    bool settingsDirty_{ true };
    RenderPipelineSettings settings_;
    /// Previous pipeline state hash.
    unsigned oldPipelineStateHash_{};

    CommonFrameInfo frameInfo_;

    SharedPtr<RenderBufferManager> renderBufferManager_;
    SharedPtr<ShadowMapAllocator> shadowMapAllocator_;
    SharedPtr<InstancingBuffer> instancingBuffer_;
    SharedPtr<SceneProcessor> sceneProcessor_;

    SharedPtr<UnorderedScenePass> opaquePass_;
    SharedPtr<BackToFrontScenePass> alphaPass_;

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
