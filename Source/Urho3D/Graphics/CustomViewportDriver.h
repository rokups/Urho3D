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

#include "../Graphics/GraphicsDefs.h"
#include "../Graphics/Drawable.h"
#include "../Graphics/PipelineStateCache.h"
#include "../Math/NumericRange.h"

#include <EASTL/algorithm.h>
#include <EASTL/vector.h>

namespace Urho3D
{

class Light;
class RenderSurface;
class VertexShader;
class PixelShader;

/// Collection of drawables in the Scene.
using DrawableCollection = ea::vector<Drawable*>;

/// Collection of geometries in the Scene.
using GeometryCollection = ea::vector<Drawable*>;

/// Collection of lights in the Scene.
using LightCollection = ea::vector<Light*>;

/// Min and max Z value of drawable(s).
using DrawableZRange = NumericRange<float>;

/// Per-viewport per-worker cache.
struct DrawableWorkerCache
{
    /// Visible geometries.
    ea::vector<Drawable*> visibleGeometries_;
    /// Visible lights.
    ea::vector<Light*> visibleLights_;
    /// Range of Z values of the geometries.
    DrawableZRange zRange_;
};

/// Per-viewport per-drawable cache.
struct DrawableViewportCache
{
    /// Underlying type of transient traits.
    using TransientTraitType = unsigned char;
    /// Transient traits.
    enum TransientTrait : TransientTraitType
    {
        /// Whether the drawable is updated.
        DrawableUpdated = 1 << 1,
        /// Whether the drawable has geometry visible from the main camera.
        DrawableVisibleGeometry = 1 << 2,
    };

    /// Transient Drawable traits, valid within the frame.
    ea::vector<TransientTraitType> drawableTransientTraits_;
    /// Drawable min and max Z values. Invalid if drawable is not visible.
    ea::vector<DrawableZRange> drawableZRanges_;

    /// Processed visible drawables.
    ea::vector<DrawableWorkerCache> workerCache_;
    /// Visible lights.
    ea::vector<Light*> visibleLights_;

    /// Reset cache in the beginning of the frame.
    void Reset(unsigned numDrawables, unsigned numWorkers)
    {
        drawableTransientTraits_.resize(numDrawables);
        drawableZRanges_.resize(numDrawables);

        workerCache_.resize(numWorkers);
        for (DrawableWorkerCache& cache : workerCache_)
            cache = {};

        visibleLights_.clear();

        // Reset transient traits
        ea::fill(drawableTransientTraits_.begin(), drawableTransientTraits_.end(), TransientTraitType{});
    }

    /// Iterate each visible light.
    template <class T> void ForEachVisibleLight(const T& callback) const
    {
        for (unsigned i = 0; i < visibleLights_.size(); ++i)
            callback(i, visibleLights_[i]);
    }
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
    /// Post task to be running from worker thread.
    virtual void PostTask(std::function<void(unsigned)> task) = 0;
    /// Wait until all posted tasks are completed.
    virtual void CompleteTasks() = 0;
    //virtual void ClearViewport(ClearTargetFlags flags, const Color& color, float depth, unsigned stencil) = 0;

    /// Collect drawables potentially visible from given camera.
    virtual void CollectDrawables(DrawableCollection& drawables, Camera* camera, DrawableFlags flags) = 0;
    /// Reset per-viewport cache in the beginning of the frame.
    virtual void ResetViewportCache(DrawableViewportCache& cache) = 0;
    /// Process drawables visible by the primary viewport camera.
    virtual void ProcessPrimaryDrawables(DrawableViewportCache& cache,
        const DrawableCollection& drawables, Camera* camera) = 0;
    /// Collect lit geometries.
    virtual void CollectLitGeometries(const DrawableViewportCache& cache,
        DrawableLightCache& lightCache, Light* light) = 0;

};

}
