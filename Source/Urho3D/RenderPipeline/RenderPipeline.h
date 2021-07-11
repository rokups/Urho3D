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
#include "../RenderPipeline/RenderPipelineDefs.h"
#include "../RenderPipeline/RenderPipelineDebugger.h"
#include "../Scene/Component.h"

namespace Urho3D
{

class RenderPipeline;
class RenderSurface;
class Viewport;

/// Base interface of render pipeline viewport instance.
class URHO3D_API RenderPipelineView
    : public Object
    , public RenderPipelineInterface
{
    URHO3D_OBJECT(RenderPipelineView, Object);

public:
    explicit RenderPipelineView(RenderPipeline* renderPipeline);
    RenderPipeline* GetRenderPipeline() const { return renderPipeline_; }

    /// Called in the beginning of the update to check if pipeline should be executed.
    virtual bool Define(RenderSurface* renderTarget, Viewport* viewport) = 0;
    /// Called for defined pipelines before rendering. Frame info is only partially filled.
    virtual void Update(const FrameInfo& frameInfo) = 0;
    /// Called for updated pipelines in appropriate order.
    virtual void Render() = 0;
    /// Return frame info with all members filled.
    virtual const FrameInfo& GetFrameInfo() const = 0;
    /// Return render pipeline statistics for profiling.
    virtual const RenderPipelineStats& GetStats() const = 0;

    /// Implement RenderPipelineInterface
    /// @{
    Context* GetContext() const override { return BaseClassName::GetContext(); }
    RenderPipelineDebugger* GetDebugger() override { return nullptr; }
    /// @}

protected:
    RenderPipeline* const renderPipeline_{};
    Graphics* const graphics_{};
    Renderer* const renderer_{};
};

/// Scene component that spawns render pipeline instances.
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
    virtual SharedPtr<RenderPipelineView> Instantiate();

    /// Invoked when settings change.
    Signal<void(const RenderPipelineSettings&)> OnSettingsChanged;

private:
    void MarkSettingsDirty();

    RenderPipelineSettings settings_;
};

}
