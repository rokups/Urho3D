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
#include "../RenderPipeline/RenderPipelineDefs.h"
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
        VisibleInCullCamera = 1 << 0,
        Lit = 1 << 1,
        ForwardLit = 1 << 2,
    };
};

struct SortedOccluder
{
    float sortValue_{};
    Drawable* drawable_{};

    bool operator<(const SortedOccluder& rhs) const { return sortValue_ < rhs.sortValue_; }
};

/// Reference to SourceBatch of Drawable geometry, with resolved material passes.
struct GeometryBatch
{
    Drawable* drawable_{};
    unsigned sourceBatchIndex_{};

    /// If deferred pass is present, unlit base, lit base and light passes are ignored.
    Pass* deferredPass_{};
    /// Unlit base pass (no per-pixel lighting, optional ambient lighting).
    Pass* unlitBasePass_{};
    /// Lit base pass (per-pixel lighting from one light source and ambient lighting).
    Pass* litBasePass_{};
    /// Additive light pass (per-pixel lighting from one light source).
    Pass* lightPass_{};
};

/// Interface of scene pass used by drawable processor.
///
/// There are 4 types of batches:
/// 1) Deferred Pass: if present, used unconditionally. Unlit base, lit base and light passes are ignored.
/// 2) Unlit Base + Lit Base + Light:
///    Batch is rendered in N passes if the first per-pixel light can be rendered together with ambient.
///    Batch is rendered in N + 1 passes otherwise.
///    N is equal to the number per-pixel lights affecting object.
/// 3) Unlit Base + Light: batch is rendered with per-pixel forward lighting in N + 1 passes.
/// 4) Unlit Base: batch is rendered once, per-pixel lighting is not applied.
///
/// Other pass combinations are invalid.
class URHO3D_API DrawableProcessorPass : public Object
{
    URHO3D_OBJECT(DrawableProcessorPass, Object);

public:
    struct AddBatchResult
    {
        bool added_{};
        bool forwardLitAdded_{};
    };

    DrawableProcessorPass(RenderPipelineInterface* renderPipeline, DrawableProcessorPassFlags flags,
        unsigned deferredPassIndex, unsigned unlitBasePassIndex, unsigned litBasePassIndex, unsigned lightPassIndex);

    AddBatchResult AddBatch(unsigned threadIndex, Drawable* drawable, unsigned sourceBatchIndex, Technique* technique);

    DrawableProcessorPassFlags GetFlags() const { return flags_; }
    bool HasLightPass() const { return lightPassIndex_ != M_MAX_UNSIGNED; }

private:
    const DrawableProcessorPassFlags flags_{};

    const unsigned deferredPassIndex_{};
    const unsigned unlitBasePassIndex_{};
    const unsigned litBasePassIndex_{};
    const unsigned lightPassIndex_{};

protected:
    /// RenderPipeline callbacks
    /// @{
    virtual void OnUpdateBegin(const CommonFrameInfo& frameInfo);
    /// @}

    WorkQueueVector<GeometryBatch> geometryBatches_;
};

/// Utility used to update and process visible or shadow caster Drawables.
class URHO3D_API DrawableProcessor : public Object
{
    URHO3D_OBJECT(DrawableProcessor, Object);

public:
    explicit DrawableProcessor(RenderPipelineInterface* renderPipeline);
    ~DrawableProcessor() override;
    void SetPasses(ea::vector<SharedPtr<DrawableProcessorPass>> passes);
    void SetSettings(const DrawableProcessorSettings& settings);

    /// RenderPipeline callbacks
    /// @{
    void OnUpdateBegin(const FrameInfo& frameInfo);
    void OnCollectStatistics(RenderPipelineStats& stats);
    /// @}

    const FrameInfo& GetFrameInfo() const { return frameInfo_; }
    const DrawableProcessorSettings& GetSettings() const { return settings_; }

    /// Process occluders. UpdateBatches for occluders may be called twice, but never reentrantly.
    void ProcessOccluders(const ea::vector<Drawable*>& occluders, float sizeThreshold);

    /// Return information about visible occluders
    /// @{
    bool HasOccluders() const { return !sortedOccluders_.empty(); }
    const auto& GetOccluders() const { return sortedOccluders_; }
    /// @}

    void ProcessVisibleDrawables(const ea::vector<Drawable*>& drawables, OcclusionBuffer* occlusionBuffer);

    /// Return information about visible geometries and lights
    /// @{
    const auto& GetGeometries() const { return geometries_; }
    const FloatRange& GetSceneZRange() const { return sceneZRange_; }

    const auto& GetLights() const { return lights_; }
    Light* GetLight(unsigned lightIndex) const { return lights_[lightIndex]; }

    const auto& GetLightProcessors() const { return lightProcessors_; }
    LightProcessor* GetLightProcessor(unsigned lightIndex) const { return lightProcessors_[lightIndex]; }

    const auto& GetLightProcessorsByShadowMap() const { return lightProcessorsByShadowMapTexture_; }
    /// @}

    /// Return information from global drawable index. May be invalid for invisible drawables.
    /// @{
    unsigned char GetGeometryRenderFlags(unsigned drawableIndex) const { return geometryFlags_[drawableIndex]; }
    const FloatRange& GetGeometryZRange(unsigned drawableIndex) const { return geometryZRanges_[drawableIndex]; }
    const LightAccumulator& GetGeometryLighting(unsigned drawableIndex) const { return geometryLighting_[drawableIndex]; }
    /// @}

    /// Internal. Pre-process shadow caster candidates. Safe to call from worker thread.
    void PreprocessShadowCasters(ea::vector<Drawable*>& shadowCasters,
        const ea::vector<Drawable*>& candidates, const FloatRange& frustumSubRange, Light* light, Camera* shadowCamera);
    /// Internal. Finalize shadow casters processing.
    void ProcessShadowCasters();

    /// Process lights: collect lit geometries, query shadow casters, update shadow maps.
    void ProcessLights(LightProcessorCallback* callback);
    /// Accumulate forward lighting for specified light source and geometries.
    void ProcessForwardLightingForLight(unsigned lightIndex, const ea::vector<Drawable*>& litGeometries);
    /// Should be called after all forward lighting is processed.
    void FinalizeForwardLighting();
    /// Process forward lighting for all lights.
    void ProcessForwardLighting();

    /// Update drawable geometries if needed.
    void UpdateGeometries();

protected:
    void ProcessVisibleDrawable(Drawable* drawable);
    void ProcessQueuedDrawable(Drawable* drawable);
    void UpdateDrawableZone(const BoundingBox& boundingBox, Drawable* drawable) const;
    void QueueDrawableUpdate(Drawable* drawable);
    void QueueDrawableGeometryUpdate(unsigned threadIndex, Drawable* drawable);
    void CheckMaterialForAuxiliaryRenderSurfaces(Material* material);

    FloatRange CalculateBoundingBoxZRange(const BoundingBox& boundingBox) const;

    void SortLightProcessorsByShadowMapSize();
    void SortLightProcessorsByShadowMapTexture();

private:
    /// Whether the drawable is already updated for this pipeline and frame.
    /// Technically copyable to allow storage in vector, but is invalidated on copying.
    struct UpdateFlag : public std::atomic_flag
    {
        UpdateFlag() = default;
        UpdateFlag(const UpdateFlag& other) {}
        UpdateFlag(UpdateFlag&& other) {}
    };

    /// External dependencies
    /// @{
    WorkQueue* workQueue_{};
    Material* defaultMaterial_{};
    /// @}

    /// Cached between frames
    /// @{
    ea::vector<SharedPtr<DrawableProcessorPass>> passes_;
    DrawableProcessorSettings settings_;
    ea::unique_ptr<LightProcessorCache> lightProcessorCache_;
    /// @}

    /// Constant within frame, changes between frames
    /// @{
    FrameInfo frameInfo_;
    unsigned numDrawables_{};

    Matrix3x4 cullCameraViewMatrix_;
    Vector3 cullCameraZAxis_;
    Vector3 cullCameraZAxisAbs_;

    MaterialQuality materialQuality_{};
    GlobalIllumination* gi_{};
    /// @}

    /// Arrays indexed with drawable index
    /// @{
    ea::vector<UpdateFlag> isDrawableUpdated_;
    ea::vector<unsigned char> geometryFlags_;
    ea::vector<FloatRange> geometryZRanges_;
    ea::vector<LightAccumulator> geometryLighting_;
    /// @}

    ea::vector<FloatRange> sceneZRangeTemp_;
    FloatRange sceneZRange_;

    ea::vector<SortedOccluder> sortedOccluders_;

    WorkQueueVector<Drawable*> geometries_;
    WorkQueueVector<Drawable*> threadedGeometryUpdates_;
    WorkQueueVector<Drawable*> nonThreadedGeometryUpdates_;

    WorkQueueVector<Light*> lightsTemp_;
    ea::vector<Light*> lights_;
    ea::vector<LightDataForAccumulator> lightDataForAccumulator_;
    ea::vector<LightProcessor*> lightProcessors_;
    ea::vector<LightProcessor*> lightProcessorsByShadowMapSize_;
    ea::vector<LightProcessor*> lightProcessorsByShadowMapTexture_;
    unsigned numShadowedLights_{};

    WorkQueueVector<Drawable*> queuedDrawableUpdates_;
};

}
