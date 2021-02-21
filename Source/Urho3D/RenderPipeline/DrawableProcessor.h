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
#include "../Graphics/GraphicsDefs.h"
#include "../Math/NumericRange.h"
#include "../RenderPipeline/LightAccumulator.h"

#include <atomic>

namespace Urho3D
{

class DrawableProcessor;
class GlobalIllumination;
class LightProcessor;
class LightProcessorCache;
class LightProcessorCallback;
class OcclusionBuffer;
class Pass;
class RenderPipelineInterface;
class Technique;
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

/// Sorted occluder type.
struct SortedOccluder
{
    /// Sorting penalty.
    float penalty_{};
    /// Occluder drawable.
    Drawable* drawable_{};
    /// Compare.
    bool operator<(const SortedOccluder& rhs) const { return penalty_ < rhs.penalty_; }
};

/// Reference to SourceBatch of Drawable geometry, with resolved material passes.
struct GeometryBatch
{
    /// Geometry.
    Drawable* geometry_{};
    /// Index of source batch within geometry.
    unsigned sourceBatchIndex_{};
    /// Unlit base pass (no direct lighting).
    Pass* unlitBasePass_{};
    /// Lit base pass (direct lighting from one light source).
    Pass* litBasePass_{};
    /// Additive light pass (direct lighting from one light source).
    Pass* lightPass_{};
};

/// Interface of scene pass used by drawable processor.
///
/// Consists of 3 sub-passes:
/// 1) Unlit Base: Render geometry without any specific light source. Ambient lighting may or may not be applied.
/// 2) Lit Base: Render geometry with single light source and ambient lighting.
/// 3) Light: Render geometry in additive mode with single light source.
///
/// There are 3 types of batches:
/// 1) Unlit Base + Lit Base + Light:
///    Batch is rendered in N passes if the first per-pixel light can be rendered together with ambient.
///    Batch is rendered in N + 1 passes otherwise.
///    N is equal to the number per-pixel lights affecting object.
/// 2) Unlit Base + Light: batch is rendered with forward lighting in N + 1 passes.
/// 3) Unlit Base: batch is rendered once.
///
/// Other combinations are invalid.
class URHO3D_API DrawableProcessorPass : public Object
{
    URHO3D_OBJECT(DrawableProcessorPass, Object);

public:
    /// Add batch result.
    struct AddResult
    {
        /// Whether the batch was added.
        bool added_{};
        /// Whether lit batch was added.
        bool litAdded_{};
    };

    /// Construct.
    DrawableProcessorPass(RenderPipelineInterface* renderPipeline, bool needAmbient,
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

    /// Geometry batches.
    WorkQueueVector<GeometryBatch> geometryBatches_;
};

/// Drawable processor settings.
struct DrawableProcessorSettings
{
    /// Material quality.
    MaterialQuality materialQuality_{ QUALITY_HIGH };
    /// Max number of vertex lights.
    unsigned maxVertexLights_{ 4 };
    /// Max number of pixel lights.
    unsigned maxPixelLights_{ 4 };

    /// Calculate pipeline state hash.
    unsigned CalculatePipelineStateHash() const { return 0; }

    /// Compare settings.
    bool operator==(const DrawableProcessorSettings& rhs) const
    {
        return materialQuality_ == rhs.materialQuality_
            && maxVertexLights_ == rhs.maxVertexLights_
            && maxPixelLights_ == rhs.maxPixelLights_;
    }

    /// Compare settings.
    bool operator!=(const DrawableProcessorSettings& rhs) const { return !(*this == rhs); }
};

/// Drawable processing utility.
class URHO3D_API DrawableProcessor : public Object
{
    URHO3D_OBJECT(DrawableProcessor, Object);

public:
    /// Construct.
    explicit DrawableProcessor(RenderPipelineInterface* renderPipeline);
    /// Destruct.
    ~DrawableProcessor() override;

    /// Set passes.
    void SetPasses(ea::vector<SharedPtr<DrawableProcessorPass>> passes);
    /// Set settings.
    void SetSettings(const DrawableProcessorSettings& settings) { settings_ = settings; }

    /// Return current frame info.
    const FrameInfo& GetFrameInfo() const { return frameInfo_; }

    /// Process and filter occluders.
    void ProcessOccluders(const ea::vector<Drawable*>& occluders, float sizeThreshold);
    /// Return whether there are active occluders.
    bool HasOccluders() const { return !sortedOccluders_.empty(); }
    /// Return active occluders.
    const auto& GetOccluders() const { return sortedOccluders_; }

    /// Process visible geometries and lights.
    void ProcessVisibleDrawables(const ea::vector<Drawable*>& drawables, OcclusionBuffer* occlusionBuffer);

    // TODO(renderer): Get rid of redundant "visible"
    /// Return visible geometries.
    const auto& GetVisibleGeometries() const { return visibleGeometries_; }
    /// Return visible lights.
    const auto& GetVisibleLights() const { return visibleLights_; }
    /// Return light processors for visible lights.
    const auto& GetLightProcessors() const { return lightProcessors_; }
    /// Return scene Z range.
    const FloatRange& GetSceneZRange() const { return sceneZRange_; }
    /// Return geometry render flags.
    unsigned char GetGeometryRenderFlags(unsigned drawableIndex) const { return geometryFlags_[drawableIndex]; }
    /// Return geometry Z range.
    const FloatRange& GetGeometryZRange(unsigned drawableIndex) const { return geometryZRanges_[drawableIndex]; }
    /// Return geometry forward lighting.
    const LightAccumulator& GetGeometryLighting(unsigned drawableIndex) const { return geometryLighting_[drawableIndex]; }
    /// Return geometry forward lighting (mutable).
    LightAccumulator& GetMutableGeometryLighting(unsigned drawableIndex) { return geometryLighting_[drawableIndex]; }
    /// Return visible light by index.
    Light* GetLight(unsigned lightIndex) const { return visibleLights_[lightIndex]; }
    /// Return light processor by index.
    LightProcessor* GetLightProcessor(unsigned lightIndex) const { return lightProcessors_[lightIndex]; }

    /// Internal. Pre-process shadow caster candidates. Safe to call from worker thread.
    void PreprocessShadowCasters(ea::vector<Drawable*>& shadowCasters,
        const ea::vector<Drawable*>& candidates, const FloatRange& frustumSubRange, Light* light, Camera* shadowCamera);
    /// Internal. Finalize shadow casters processing.
    void ProcessShadowCasters();

    /// Process lights: collect lit geometries, query shadow casters, update shadow maps.
    void ProcessLights(LightProcessorCallback* callback);
    /// Accumulate forward lighting for specified light source and geometries.
    void ProcessForwardLighting(unsigned lightIndex, const ea::vector<Drawable*>& litGeometries);

    /// Update drawable geometries if needed.
    void UpdateGeometries();

protected:
    /// Called when update begins.
    void OnUpdateBegin(const FrameInfo& frameInfo);
    /// Process visible drawable.
    void ProcessVisibleDrawable(Drawable* drawable);
    /// Process queued invisible drawable.
    void ProcessQueuedDrawable(Drawable* drawable);
    /// Update zone of drawable.
    void UpdateDrawableZone(const BoundingBox& boundingBox, Drawable* drawable) const;
    /// Queue drawable update. Ignored if already updated or queued. Safe to call from worker thread.
    void QueueDrawableUpdate(Drawable* drawable);
    /// Queue drawable geometry update. Safe to call from worker thread.
    void QueueDrawableGeometryUpdate(unsigned threadIndex, Drawable* drawable);
    /// Calculate Z range of bounding box.
    FloatRange CalculateBoundingBoxZRange(const BoundingBox& boundingBox) const;
    /// Return light processors sorted by shadow map sizes.
    void SortLightProcessorsByShadowMap();

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
    /// Default material.
    Material* defaultMaterial_{};

    /// Scene passes sinks.
    ea::vector<SharedPtr<DrawableProcessorPass>> passes_;
    /// Settings.
    DrawableProcessorSettings settings_;

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

    /// Sorted occluders.
    ea::vector<SortedOccluder> sortedOccluders_;

    /// Visible geometries.
    WorkQueueVector<Drawable*> visibleGeometries_;
    /// Geometries to be updated from worker threads.
    WorkQueueVector<Drawable*> threadedGeometryUpdates_;
    /// Geometries to be updated from main thread.
    WorkQueueVector<Drawable*> nonThreadedGeometryUpdates_;

    /// Visible lights (temporary collection for threading).
    WorkQueueVector<Light*> visibleLightsTemp_;
    /// Visible lights.
    ea::vector<Light*> visibleLights_;
    /// Light processors for visible lights.
    ea::vector<LightProcessor*> lightProcessors_;
    /// Light processor cache.
    ea::unique_ptr<LightProcessorCache> lightProcessorCache_;

    /// Delayed drawable updates.
    WorkQueueVector<Drawable*> queuedDrawableUpdates_{};

    /// Light processors for visible lights sorted by shadow map sizes.
    ea::vector<LightProcessor*> lightProcessorsByShadowMapSize_;
};

}
