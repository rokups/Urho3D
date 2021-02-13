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

#include "../Graphics/Drawable.h"
#include "../RenderPipeline/BatchCompositor.h"

namespace Urho3D
{

class DrawableProcessor;
class RenderPipelineInterface;
class RenderSurface;
class Viewport;

/// Scene processor for RenderPipeline
class URHO3D_API SceneProcessor : public Object
{
    URHO3D_OBJECT(SceneProcessor, Object);

public:
    /// Construct.
    SceneProcessor(RenderPipelineInterface* renderPipeline, const ea::string& shadowTechnique);
    /// Destruct.
    ~SceneProcessor() override;

    /// Define before RenderPipeline update.
    void Define(RenderSurface* renderTarget, Viewport* viewport);
    /// Set passes.
    void SetPasses(ea::vector<SharedPtr<BatchCompositorPass>> passes);

    /// Update frame info.
    void UpdateFrameInfo(const FrameInfo& frameInfo);
    /// Update drawables and batches.
    void Update();

    /// Return whether is valid.
    bool IsValid() const { return frameInfo_.camera_ && frameInfo_.octree_; }
    /// Return frame info.
    const FrameInfo& GetFrameInfo() const { return frameInfo_; }
    /// Return whether the object corresponds to shadow pass.
    bool IsShadowPass(Object* pass) const { return batchCompositor_ == pass; }
    /// Return drawable processor.
    DrawableProcessor* GetDrawableProcessor() const { return drawableProcessor_; }

private:
    /// Called when update begins.
    void OnUpdateBegin(const FrameInfo& frameInfo);

    /// Render pipeline interface.
    RenderPipelineInterface* renderPipeline_{};

    /// Drawable processor.
    SharedPtr<DrawableProcessor> drawableProcessor_;
    /// Batch compositor.
    SharedPtr<BatchCompositor> batchCompositor_;

    /// Frame info.
    FrameInfo frameInfo_;

    /// Occluders in current frame.
    ea::vector<Drawable*> occluders_;
    /// Drawables in current frame.
    ea::vector<Drawable*> drawables_;
};

}
