//
// Copyright (c) 2008-2020 the Urho3D project.
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
#include "../Graphics/CustomViewportDriver.h"
#include "../Graphics/CustomViewportScript.h"
#include "../Graphics/Drawable.h"

namespace Urho3D
{

class Camera;
class RenderPath;
class RenderSurface;
class Scene;
class XMLFile;
class View;
class Viewport;

///
class URHO3D_API CustomView : public Object, private CustomViewportDriver
{
    URHO3D_OBJECT(CustomView, Object);

public:
    /// Construct with defaults.
    CustomView(Context* context, CustomViewportScript* script);
    /// Destruct.
    ~CustomView() override;

    /// Register object with the engine.
    static void RegisterObject(Context* context);

    /// Define all dependencies.
    bool Define(RenderSurface* renderTarget, Viewport* viewport);

    /// Update.
    void Update(const FrameInfo& frameInfo);

    /// Render.
    void Render();

protected:
    unsigned GetNumThreads() const override { return numThreads_; }
    void PostTask(std::function<void(unsigned)> task) override;
    void CompleteTasks() override;

    //void ClearViewport(ClearTargetFlags flags, const Color& color, float depth, unsigned stencil) override;
    void CollectDrawables(DrawableCollection& drawables, Camera* camera, DrawableFlags flags) override;
    void ProcessPrimaryDrawables(DrawableViewportCache& viewportCache,
        const DrawableCollection& drawables, Camera* camera) override;
    void CollectLitGeometries(const DrawableViewportCache& viewportCache,
        DrawableLightCache& lightCache, Light* light) override;

private:
    Graphics* graphics_{};
    WorkQueue* workQueue_{};
    WeakPtr<CustomViewportScript> script_;

    Scene* scene_{};
    Camera* camera_{};
    Octree* octree_{};
    RenderSurface* renderTarget_{};
    Viewport* viewport_{};

    unsigned numThreads_{ 1 };
    unsigned numDrawables_{};

    FrameInfo frameInfo_{};
};

}
