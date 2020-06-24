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
#include "../Graphics/Graphics.h"
#include "../Graphics/Light.h"
#include "../Graphics/Technique.h"
#include "../Math/NumericRange.h"
#include "../Math/SphericalHarmonics.h"

#include <EASTL/fixed_vector.h>
#include <EASTL/vector_map.h>

namespace Urho3D
{

/// Min and max Z value of drawable(s).
using DrawableZRange = NumericRange<float>;

/// Min and max Z value of scene. Can be used from multiple threads.
class SceneZRange
{
public:
    /// Clear in the beginning of the frame.
    void Clear(unsigned numThreads)
    {
        threadRanges_.clear();
        threadRanges_.resize(numThreads);
        sceneRangeDirty_ = true;
    }

    /// Accumulate min and max Z value.
    void Accumulate(unsigned threadIndex, const DrawableZRange& range)
    {
        threadRanges_[threadIndex] |= range;
    }

    /// Get value.
    const DrawableZRange& Get()
    {
        if (sceneRangeDirty_)
        {
            sceneRangeDirty_ = false;
            sceneRange_ = {};
            for (const DrawableZRange& range : threadRanges_)
                sceneRange_ |= range;
        }
        return sceneRange_;
    }

private:
    /// Min and max Z value per thread.
    ea::vector<DrawableZRange> threadRanges_;
    /// Min and max Z value for Scene.
    DrawableZRange sceneRange_;
    /// Whether the Scene range is dirty.
    bool sceneRangeDirty_{};
};

/// Transient drawable data, indexed via drawable index. Doesn't persist across frames.
struct TransientDrawableIndex
{
    /// Underlying type of traits.
    using TraitType = unsigned char;
    /// Traits.
    enum Trait : TraitType
    {
        /// Whether the drawable is updated.
        DrawableUpdated = 1 << 1,
        /// Whether the drawable has geometry visible from the main camera.
        DrawableVisibleGeometry = 1 << 2,
        /// Whether the drawable is lit using forward rendering.
        ForwardLit = 1 << 3,
    };

    /// Traits.
    ea::vector<TraitType> traits_;
    /// Drawable min and max Z values. Invalid if drawable is not updated.
    ea::vector<DrawableZRange> zRange_;

    /// Reset cache in the beginning of the frame.
    void Reset(unsigned numDrawables)
    {
        traits_.resize(numDrawables);
        ea::fill(traits_.begin(), traits_.end(), TraitType{});

        zRange_.resize(numDrawables);
    }
};

/// Type of scene pass.
enum class ScenePassType
{
    /// No forward lighting.
    /// Object is rendered once in base pass.
    Unlit,
    /// Forward lighting pass.
    /// Object with lighting from the first light rendered once in base pass.
    /// Lighting from other lights is applied in additional passes.
    ForwardLitBase,
    /// Forward lighting pass.
    /// Object is rendered once in base pass without lighting.
    /// Lighting from all lights is applied in additional passes.
    ForwardUnlitBase
};

/// Description of scene pass.
struct ScenePassDescription
{
    /// Pass type.
    ScenePassType type_{};
    /// Material pass used to render materials that don't receive light.
    ea::string unlitBasePassName_;
    /// Material pass used for first light during forward rendering.
    ea::string litBasePassName_;
    /// Material pass used for the rest of lights during forward rendering.
    ea::string additionalLightPassName_;
};

/// Context used for light accumulation.
struct DrawableLightDataAccumulationContext
{
    /// Max number of pixel lights
    unsigned maxPixelLights_{ 1 };
    /// Light importance.
    LightImportance lightImportance_{};
    /// Light index.
    unsigned lightIndex_{};
    /// Array of lights to be indexed.
    const ea::vector<Light*>* lights_;
};

/// Accumulated light data for drawable.
/// MaxPixelLights: Max number of per-pixel lights supported. Important lights may override this limit.
/// MaxVertexLights: Max number of per-vertex lights supported.
template <unsigned MaxPixelLights, unsigned MaxVertexLights>
struct DrawableLightData
{
    /// Max number of lights that don't require allocations.
    static const unsigned NumElements = ea::max(MaxPixelLights + 1, 4u) + MaxVertexLights;
    /// Container for lights.
    using Container = ea::vector_map<float, unsigned, ea::less<float>, ea::allocator,
        ea::fixed_vector<ea::pair<float, unsigned>, NumElements>>;
    /// Container for vertex lights.
    using VertexLightContainer = ea::array<unsigned, MaxVertexLights>;

    /// Reset accumulator.
    void Reset()
    {
        lights_.clear();
        numImportantLights_ = 0;
    }

    /// Accumulate light.
    void AccumulateLight(const DrawableLightDataAccumulationContext& ctx, float penalty)
    {
        // Count important lights
        if (ctx.lightImportance_ == LI_IMPORTANT)
        {
            penalty = -1.0f;
            ++numImportantLights_;
        }

        // Add new light
        lights_.emplace(penalty, ctx.lightIndex_);

        // If too many lights, drop the least important one
        firstVertexLight_ = ea::max(ctx.maxPixelLights_, numImportantLights_);
        const unsigned maxLights = MaxVertexLights + firstVertexLight_;
        if (lights_.size() > maxLights)
        {
            // TODO: Update SH
            lights_.pop_back();
        }
    }

    /// Return per-vertex lights.
    VertexLightContainer GetVertexLights() const
    {
        VertexLightContainer vertexLights;
        for (unsigned i = 0; i < 4; ++i)
        {
            const unsigned index = i + firstVertexLight_;
            vertexLights[i] = index < lights_.size() ? lights_.at(index).second : M_MAX_UNSIGNED;
        }
        return vertexLights;
    }

    /// Container of per-pixel and per-pixel lights.
    Container lights_;
    /// Accumulated SH lights.
    SphericalHarmonicsDot9 sh_;
    /// Number of important lights.
    unsigned numImportantLights_{};
    /// First vertex light.
    unsigned firstVertexLight_{};
};

/// Scene batch for specific sub-pass.
struct SceneBatch
{
    /// Drawable index.
    unsigned drawableIndex_{};
    /// Source batch index.
    unsigned sourceBatchIndex_{};
    /// Drawable to be rendered.
    Drawable* drawable_{};
    /// Geometry to be rendered.
    Geometry* geometry_{};
    /// Material to be rendered.
    Material* material_{};
    /// Material pass to be rendered.
    Pass* pass_{};
    /// Pipeline state.
    PipelineState* pipelineState_{};
};

/// Pipeline state factory for scene.
class ScenePipelineStateFactory
{
public:
    /// Create pipeline state. Only fields that constribute to pipeline state hashes are safe to use.
    virtual PipelineState* CreatePipelineState(Camera* camera, Drawable* drawable,
        Geometry* geometry, Material* material, Pass* pass) = 0;
};

/// Utility class to collect batches from the scene for given frame.
class SceneBatchCollector : public Object
{
    URHO3D_OBJECT(SceneBatchCollector, Object);
public:
    /// Max number of vertex lights.
    static const unsigned MaxVertexLights = 4;
    /// Max optimal number of pixel lights.
    static const unsigned MaxPixelLights = 4;

    /// Construct.
    SceneBatchCollector(Context* context);
    /// Destruct.
    ~SceneBatchCollector();

    /// Process drawables in frame.
    void Process(const FrameInfo& frameInfo, ScenePipelineStateFactory& pipelineStateFactory,
        ea::span<const ScenePassDescription> passes, const ea::vector<Drawable*>& drawables)
    {
        InitializeFrame(frameInfo, pipelineStateFactory);
        InitializePasses(passes);
        UpdateAndCollectSourceBatches(drawables);
        ProcessVisibleLights();
        CollectSceneBatches();
    }

    /// Return main light.
    Light* GetMainLight() const { return mainLight_; }
    /// Return base batches for given pass.
    const ea::vector<SceneBatch>& GetBaseBatches(const ea::string& pass) const;
    /// Return vertex lights for drawable (as indices in the array of visible lights).
    ea::array<unsigned, MaxVertexLights> GetVertexLightIndices(unsigned drawableIndex) const { return drawableLighting_[drawableIndex].GetVertexLights(); }
    /// Return vertex lights for drawable (as pointers).
    ea::array<Light*, MaxVertexLights> GetVertexLights(unsigned drawableIndex) const;

private:
    /// Batch of drawable in scene.
    struct IntermediateSceneBatch;
    /// Sub-pass pipeline state cache context.
    struct SubPassPipelineStateContext;
    /// Sub-pass pipeline state cache key.
    struct SubPassPipelineStateKey;
    /// Sub-pass pipeline state cache entry.
    struct SubPassPipelineStateEntry;
    /// Sub-pass pipeline state cache.
    struct SubPassPipelineStateCache;
    /// Internal pass data.
    struct PassData;
    /// Helper class to evaluate min and max Z of the drawable.
    struct DrawableZRangeEvaluator;
    /// Internal light data.
    struct LightData;

    /// Return technique for given material and drawable.
    Technique* FindTechnique(Drawable* drawable, Material* material) const;

    /// Reset collection in the begining of the frame.
    void InitializeFrame(const FrameInfo& frameInfo, ScenePipelineStateFactory& pipelineStateFactory);
    /// Initialize passes.
    void InitializePasses(ea::span<const ScenePassDescription> passes);

    /// Update source batches and collect pass batches.
    void UpdateAndCollectSourceBatches(const ea::vector<Drawable*>& drawables);
    /// Update source batches and collect pass batches for single thread.
    void UpdateAndCollectSourceBatchesForThread(unsigned threadIndex, ea::span<Drawable* const> drawables);

    /// Process visible lights.
    void ProcessVisibleLights();
    /// Process light in worker thread.
    void ProcessLightThreaded(Light* light, LightData& lightData);
    /// Collect lit geometries.
    void CollectLitGeometries(Light* light, LightData& lightData);
    /// Accumulate forward lighting for given light.
    void AccumulateForwardLighting(unsigned lightIndex);

    /// Collect scene batches.
    void CollectSceneBatches();
    /// Convert scene batches from intermediate batches to base batches.
    void CollectSceneBaseBatches(SubPassPipelineStateCache& subPassCache, bool isLit,
        const ThreadedVector<IntermediateSceneBatch>& intermediateBatches, ea::vector<SceneBatch>& sceneBatches);

    /// Min number of processed drawables in single task.
    unsigned drawableWorkThreshold_{ 1 };
    /// Min number of processed lit geometries in single task.
    unsigned litGeometriesWorkThreshold_{ 1 };
    /// Min number of processed batches in single task.
    unsigned batchWorkThreshold_{ 1 };

    /// Work queue.
    WorkQueue* workQueue_{};
    /// Renderer.
    Renderer* renderer_{};
    /// Pipeline state factory.
    ScenePipelineStateFactory* pipelineStateFactory_{};
    /// Number of worker threads.
    unsigned numThreads_{};
    /// Material quality.
    MaterialQuality materialQuality_{};

    /// Frame info.
    FrameInfo frameInfo_;
    /// Octree.
    Octree* octree_{};
    /// Camera.
    Camera* camera_{};
    /// Number of drawables.
    unsigned numDrawables_{};

    /// Passes.
    ea::vector<PassData> passes_;
    /// Base batches lookup table.
    ea::unordered_map<unsigned, ea::vector<SceneBatch>*> baseBatchesLookup_;

    /// Visible geometries.
    ThreadedVector<Drawable*> visibleGeometries_;
    /// Temporary thread-safe collection of visible lights.
    ThreadedVector<Light*> visibleLightsTemp_;
    /// Visible lights.
    ea::vector<Light*> visibleLights_;
    /// Main directional light.
    Light* mainLight_{};
    /// Scene Z range.
    SceneZRange sceneZRange_;

    /// Transient data index.
    TransientDrawableIndex transient_;
    /// Drawable lighting data index.
    ea::vector<DrawableLightData<MaxPixelLights, MaxVertexLights>> drawableLighting_;

    /// Per-light caches.
    ea::unordered_map<WeakPtr<Light>, ea::unique_ptr<LightData>> cachedLightData_;
    /// Per-light caches for visible lights.
    ea::vector<LightData*> visibleLightsData_;

    /// Temporary collection for pipeline state cache misses.
    ThreadedVector<SceneBatch*> sceneBatchesWithoutPipelineStates_;
};

}
