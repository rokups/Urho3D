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
#include "../RenderPipeline/RenderPipelineInterface.h"
#include "../RenderPipeline/ScenePass.h"
#include "../Scene/Serializable.h"

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
class DrawCommandQueue;
class DrawableProcessor;
class SceneBatchCollector;
class BatchRenderer;

///
struct RenderPipelineSettings : public SceneProcessorSettings
{
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

    /// Return default draw queue. Is not automatically executed.
    DrawCommandQueue* GetDefaultDrawQueue() override { return drawQueue_; }

protected:
    /// Recalculate hash (must not be non zero). Shall be save to call from multiple threads as long as the object is not changing.
    unsigned RecalculatePipelineStateHash() const override;

    SharedPtr<PipelineState> CreateBatchPipelineState(
        const BatchStateCreateKey& key, const BatchStateCreateContext& ctx) override;
    /// Return new or existing pipeline state for deferred light volume.
    SharedPtr<PipelineState> CreateLightVolumePipelineState(LightProcessor* sceneLight, Geometry* lightGeometry);

private:
    Graphics* graphics_{};
    Renderer* renderer_{};
    WorkQueue* workQueue_{};

    /// Pipeline settings from previous frame.
    RenderPipelineSettings previousSettings_;
    /// Current pipeline settings.
    RenderPipelineSettings settings_;
    /// Current frame info.
    //FrameInfo frameInfo_{};
    /// Previous pipeline state hash.
    unsigned oldPipelineStateHash_{};

    /// Default draw queue.
    SharedPtr<DrawCommandQueue> drawQueue_;
    /// Render buffer manager.
    SharedPtr<RenderBufferManager> renderBufferManager_;
    /// Scene processor.
    SharedPtr<SceneProcessor> sceneProcessor_;
    /// Main camera of render pipeline.
    SharedPtr<CameraProcessor> cameraProcessor_;
    /// Viewport color texture handler.
    //SharedPtr<RenderBuffer> viewportColor_;
    /// Viewport depth stencil texture handler.
    //SharedPtr<RenderBuffer> viewportDepth_;

    SharedPtr<OpaqueForwardLightingScenePass> basePass_;
    SharedPtr<AlphaForwardLightingScenePass> alphaPass_;
    SharedPtr<UnlitScenePass> deferredPass_;

    //SharedPtr<RenderBuffer> deferredFinal_;
    SharedPtr<RenderBuffer> deferredAlbedo_;
    SharedPtr<RenderBuffer> deferredNormal_;
    //SharedPtr<RenderBuffer> deferredDepth_;
};

}
