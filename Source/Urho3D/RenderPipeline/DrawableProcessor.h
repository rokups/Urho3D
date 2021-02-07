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
#include "../Core/WorkQueue.h"
#include "../Container/FlagSet.h"
#include "../Math/NumericRange.h"
#include "../Graphics/GraphicsDefs.h"
#include "../RenderPipeline/LightAccumulator.h"

#include <atomic>

namespace Urho3D
{

class RenderPipeline;
class GlobalIllumination;
struct FrameInfo;

/// Flags related to geometry rendering.
/// Use old enum for simplified flag manipulation, avoid FlagSet for quick reset.
struct GeometryRenderFlag
{
    enum Type : unsigned char
    {
        /// Whether the geometry is visible in the cull camera.
        Visible = 1 << 0,
        /// Whether the geometry is lit in any way.
        Lit = 1 << 1,
        /// Whether the geometry is lit using forward rendering.
        ForwardLit = 1 << 2,
    };
};

/// Reference to SourceBatch of Drawable geometry, with resolved material passes.
struct GeometryBatch
{
    /// Geometry.
    Drawable* geometry_{};
    /// Index of source batch within geometry.
    unsigned sourceBatchIndex_{};
    /// Base pass.
    Pass* basePass_{};
    /// Additive light pass for forward rendering.
    Pass* lightPass_{};
};

/// Base interface of scene rendering pass.
///
/// Consists of 3 sub-passes:
/// 1) Unlit Base: Render geometry without any specific light source. Ambient lighting may or may not be applied.
/// 2) Lit Base: Render geometry with single light source and ambient lighting.
/// 3) Light: Render geometry in additive mode with single light source.
///
/// There are 3 types of batches:
/// 1) Lit Base + Light: batch is rendered with forward lighting in N passes, where N is number of per-pixel lights.
/// 2) Unlit Base + Light: batch is rendered with forward lighting in N + 1 passes.
/// 3) Unlit Base: batch is rendered once.
///
/// Other combinations are invalid.
class SceneRenderingPass : public Object
{
    URHO3D_OBJECT(SceneRenderingPass, Object);

public:
    /// Add batch result.
    struct AddResult
    {
        bool unlitAdded_{};
        bool litAdded_{};
    };

    /// Construct.
    SceneRenderingPass(RenderPipeline* renderPipeline, bool needAmbient,
        unsigned unlitBasePassIndex, unsigned litBasePassIndex, unsigned lightPassIndex);

    /// Add source batch of drawable. Return whether forward lit batch was added.
    AddResult AddBatch(unsigned threadIndex, Drawable* drawable, unsigned sourceBatchIndex, Technique* technique);

    /// Return whether the pass needs ambient light.
    bool NeedAmbient() const { return needAmbient_; }

private:
    /// Whether the pass is lit.
    const bool needAmbient_{};
    /// Unlit base pass index.
    const unsigned unlitBasePassIndex_{};
    /// Lit base pass index.
    const unsigned litBasePassIndex_{};
    /// Additional light pass index.
    const unsigned lightPassIndex_{};

protected:
    /// Called when update begins.
    virtual void OnUpdateBegin(const FrameInfo& frameInfo);

    /// Unlit batches. Light pass is always empty.
    WorkQueueVector<GeometryBatch> unlitBatches_;
    /// Lit batches. Base pass may be empty. Light pass cannot be empty.
    WorkQueueVector<GeometryBatch> litBatches_;
};

/// Drawable processing utility.
class URHO3D_API DrawableProcessor : public Object
{
    URHO3D_OBJECT(DrawableProcessor, Object);

public:
    /// Construct.
    explicit DrawableProcessor(RenderPipeline* renderPipeline);
    /// Set passes.
    void SetPasses(ea::vector<SharedPtr<SceneRenderingPass>> scenePasses);

    /// Process visible geometries and lights.
    void ProcessVisibleDrawables(const ea::vector<Drawable*>& drawables);
    /// Return visible geometries.
    const auto& GetVisibleGeometries() const { return visibleGeometries_; }
    /// Return visible lights.
    const auto& GetVisibleLights() const { return visibleLights_; }
    /// Return scene Z range.
    const FloatRange& GetSceneZRange() const { return sceneZRange_; }
    /// Return geometry render flags.
    unsigned char GetGeometryRenderFlags(unsigned drawableIndex) const { return geometryFlags_[drawableIndex]; }
    /// Return geometry Z range.
    const FloatRange& GetGeometryZRange(unsigned drawableIndex) const { return geometryZRanges_[drawableIndex]; }

    /// Process geometry updates.
    void ProcessGeometryUpdates();

    // TODO(renderer): Remove me
    std::atomic_flag& GET_UPDATED(unsigned drawableIndex) { return isDrawableUpdated_[drawableIndex]; }
    auto& GET_LIGHT() { return geometryLighting_; }
    auto& GET_TGU() { return threadedGeometryUpdates_; }
    auto& GET_NTGU() { return nonThreadedGeometryUpdates_; }

protected:
    /// Called when update begins.
    void OnUpdateBegin(const FrameInfo& frameInfo);
    /// Process drawable.
    void ProcessDrawable(Drawable* drawable);
    /// Calculate Z range of bounding box.
    FloatRange CalculateBoundingBoxZRange(const BoundingBox& boundingBox) const;

private:
    /// Whether the drawable is already updated for this pipeline and frame.
    /// Technically copyable to allow storage in vector, but is invalidated on copying.
    struct UpdateFlag : public std::atomic_flag
    {
        UpdateFlag() = default;
        UpdateFlag(const UpdateFlag& other) {}
        UpdateFlag(UpdateFlag&& other) {}
    };

    /// Work queue.
    WorkQueue* workQueue_{};
    /// Renderer.
    Renderer* renderer_{};
    /// Default material.
    Material* defaultMaterial_{};

    /// Scene passes sinks.
    ea::vector<SharedPtr<SceneRenderingPass>> scenePasses_;

    /// Frame info.
    FrameInfo frameInfo_;
    /// Total number of drawables in scene.
    unsigned numDrawables_{};
    /// View matrix for cull camera.
    Matrix3x4 viewMatrix_;
    /// Z axis direction.
    Vector3 viewZ_;
    /// Adjusted Z axis direction for bounding box size evaluation.
    Vector3 absViewZ_;
    /// Material quality.
    MaterialQuality materialQuality_{};
    /// Global illumination.
    GlobalIllumination* gi_{};

    /// Z-range of scene (temporary collection for threading).
    ea::vector<FloatRange> sceneZRangeTemp_;
    /// Z-range of scene.
    FloatRange sceneZRange_;

    /// Updated drawables.
    ea::vector<UpdateFlag> isDrawableUpdated_;
    /// Geometry flags. Unspecified for other drawables.
    ea::vector<unsigned char> geometryFlags_;
    /// Z-ranges of drawables. Unspecified for invisible drawables.
    ea::vector<FloatRange> geometryZRanges_;
    /// Accumulated drawable lighting. Unspecified for invisible or unlit drawables.
    ea::vector<LightAccumulator> geometryLighting_;

    /// Visible geometries.
    WorkQueueVector<Drawable*> visibleGeometries_;
    /// Geometries to be updated from worker threads.
    WorkQueueVector<Drawable*> threadedGeometryUpdates_;
    /// Geometries to be updated from main thread.
    WorkQueueVector<Drawable*> nonThreadedGeometryUpdates_;

    /// Visible lights.
    WorkQueueVector<Light*> visibleLights_;
};

}
