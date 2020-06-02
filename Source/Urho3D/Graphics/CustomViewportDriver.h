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

#include "../Core/ThreadedVector.h"
#include "../Graphics/GraphicsDefs.h"
#include "../Graphics/Drawable.h"
#include "../Graphics/PipelineState.h"
#include "../Graphics/Detail/RenderingContainers.h"
#include "../Math/NumericRange.h"

#include <EASTL/algorithm.h>
#include <EASTL/vector.h>
#include <EASTL/fixed_vector.h>

namespace Urho3D
{

class Light;
class RenderSurface;
class VertexShader;
class PixelShader;

/// Per-viewport result of drawable processing.
struct DrawableViewportCache
{
    /// Visible geometries.
    ThreadedGeometryCollection visibleGeometries_;
    /// Visible lights.
    ThreadedLightCollection visibleLights_;
    /// Scene Z range.
    SceneZRange sceneZRange_;
    /// Transient data index.
    TransientDrawableDataIndex transient_;
};

/// Per-viewport per-light cache.
struct DrawableLightCache
{
    /// Lit geometries.
    // TODO: Optimize for the case when all visible geometries are lit
    ea::vector<Drawable*> litGeometries_;
};

struct MaterialCacheKey
{
    Material* material_{};
    Geometry* geometry_{};
    Light* light_{};
};

/*class PipelineStateFactory
{
public:
    virtual PipelineState* CreatePipelineState()
};*/

class MaterialCachePerPass
{
public:
    PipelineState* GetPipelineState();
};

class CustomViewportDriver
{
public:
    /// Return number of worker threads.
    virtual unsigned GetNumThreads() const = 0;
    /// Post task to be running from worker thread.
    virtual void PostTask(std::function<void(unsigned)> task) = 0;
    /// Wait until all posted tasks are completed.
    virtual void CompleteTasks() = 0;
    //virtual void ClearViewport(ClearTargetFlags flags, const Color& color, float depth, unsigned stencil) = 0;

    /// Collect drawables potentially visible from given camera.
    virtual void CollectDrawables(DrawableCollection& drawables, Camera* camera, DrawableFlags flags) = 0;
    /// Process drawables visible by the primary viewport camera.
    virtual void ProcessPrimaryDrawables(DrawableViewportCache& viewportCache,
        const DrawableCollection& drawables, Camera* camera) = 0;
    /// Collect lit geometries.
    virtual void CollectLitGeometries(const DrawableViewportCache& viewportCache,
        DrawableLightCache& lightCache, Light* light) = 0;

};

}
