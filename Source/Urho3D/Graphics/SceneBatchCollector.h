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
#include "../Graphics/Material.h"
#include "../Graphics/Technique.h"
#include "../Math/NumericRange.h"
#include "../Math/SphericalHarmonics.h"

#include <EASTL/fixed_vector.h>
#include <EASTL/sort.h>
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
    /// No forward lighting. Custom lighting is used instead (e.g. deferred lighting).
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

    /// Return per-pixel lights.
    ea::span<const ea::pair<float, unsigned>> GetPixelLights() const
    {
        return { lights_.data(), firstVertexLight_ };
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

/// Base or lit base scene batch for specific sub-pass.
struct SceneBatch
{
    /// Drawable index.
    unsigned drawableIndex_{};
    /// Source batch index.
    unsigned sourceBatchIndex_{};
    /// Geometry type used.
    GeometryType geometryType_{};
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

    /// Return source batch.
    const SourceBatch& GetSourceBatch() const { return drawable_->GetBatches()[sourceBatchIndex_]; }
};

/// Additional light scene batch for specific sub-pass.
struct LightSceneBatch
{
    /// Base scene batch.
    SceneBatch* sceneBatch_{};
    /// Pass override.
    Pass* pass_{};
    /// Pipeline state override.
    PipelineState* pipelineState_{};
    /// Light.
    Light* light_{};
};

/// Scene batch sorted by pipeline state, material and geometry. Also sorted front to back.
struct SceneBatchSortedByState
{
    /// Sorting value for pipeline state.
    unsigned long long pipelineStateKey_{};
    /// Sorting value for material and geometry.
    unsigned long long materialGeometryKey_{};
    /// Sorting distance.
    float distance_{};
    /// Batch to be sorted.
    const SceneBatch* sceneBatch_{};

    /// Construct default.
    SceneBatchSortedByState() = default;

    /// Construct from batch.
    explicit SceneBatchSortedByState(const SceneBatch* batch)
        : sceneBatch_(batch)
    {
        const SourceBatch& sourceBatch = batch->GetSourceBatch();

        // 8: render order
        // 32: shader variation
        // 24: pipeline state
        const unsigned long long renderOrder = batch->material_->GetRenderOrder();
        const unsigned long long shaderHash = batch->pipelineState_->GetShaderHash();
        const unsigned pipelineStateHash = MakeHash(batch->pipelineState_);
        pipelineStateKey_ |= renderOrder << 56;
        pipelineStateKey_ |= shaderHash << 24;
        pipelineStateKey_ |= (pipelineStateHash & 0xffffff) ^ (pipelineStateHash >> 24);

        // 32: material
        // 32: geometry
        unsigned long long materialHash = MakeHash(batch->material_);
        materialGeometryKey_ |= (materialHash ^ sourceBatch.lightmapIndex_) << 32;
        materialGeometryKey_ |= MakeHash(batch->geometry_);

        distance_ = sourceBatch.distance_;
    }

    /// Compare sorted batches.
    bool operator < (const SceneBatchSortedByState& rhs) const
    {
        if (pipelineStateKey_ != rhs.pipelineStateKey_)
            return pipelineStateKey_ < rhs.pipelineStateKey_;
        if (materialGeometryKey_ != rhs.materialGeometryKey_)
            return materialGeometryKey_ < rhs.materialGeometryKey_;
        return distance_ > rhs.distance_;
    }
};

/// Scene batch sorted by render order and back to front.
struct SceneBatchSortedBackToFront
{
    /// Render order.
    unsigned char renderOrder_{};
    /// Sorting distance.
    float distance_{};
    /// Batch to be sorted.
    const SceneBatch* sceneBatch_{};

    /// Construct default.
    SceneBatchSortedBackToFront() = default;

    /// Construct from batch.
    explicit SceneBatchSortedBackToFront(const SceneBatch* batch)
        : renderOrder_(batch->material_->GetRenderOrder())
        , sceneBatch_(batch)
    {
        const SourceBatch& sourceBatch = batch->GetSourceBatch();
        distance_ = sourceBatch.distance_;
    }

    /// Compare sorted batches.
    bool operator < (const SceneBatchSortedBackToFront& rhs) const
    {
        if (renderOrder_ != rhs.renderOrder_)
            return renderOrder_ < rhs.renderOrder_;
        return distance_ < rhs.distance_;
    }
};

/// Light batch sorted by light, pipeline state, material and geometry.
struct LightBatchSortedByState : public SceneBatchSortedByState
{
    /// Light batch.
    const LightSceneBatch* lightBatch_{};
    /// Light.
    Light* light_{};

    /// Construct default.
    LightBatchSortedByState() = default;

    /// Construct from batch.
    explicit LightBatchSortedByState(const LightSceneBatch* lightBatch)
        : SceneBatchSortedByState(lightBatch->sceneBatch_)
        , lightBatch_(lightBatch)
        , light_(lightBatch->light_)
    {
    }

    /// Compare sorted batches.
    bool operator < (const LightBatchSortedByState& rhs) const
    {
        if (light_ != rhs.light_)
            return light_ < rhs.light_;
        if (pipelineStateKey_ != rhs.pipelineStateKey_)
            return pipelineStateKey_ < rhs.pipelineStateKey_;
        return materialGeometryKey_ < rhs.materialGeometryKey_;
    }
};

/// Pipeline state factory for scene.
class ScenePipelineStateFactory
{
public:
    /// Create pipeline state. Only fields that constribute to pipeline state hashes are safe to use.
    virtual PipelineState* CreatePipelineState(Camera* camera, Drawable* drawable,
        Geometry* geometry, GeometryType geometryType, Material* material, Pass* pass, Light* light) = 0;
};

/// Utility class to collect batches from the scene for given frame.
class SceneBatchCollector : public Object
{
    URHO3D_OBJECT(SceneBatchCollector, Object);
public:
    /// Max number of vertex lights.
    static const unsigned MaxVertexLights = 4;
    /// Max number of pixel lights. Soft limit, violation leads to performance penalty.
    static const unsigned MaxPixelLights = 4;
    /// Max number of scene passes. Soft limit, violation leads to performance penalty.
    static const unsigned MaxScenePasses = 8;
    /// Collection of vertex lights used (indices).
    using VertexLightCollection = ea::array<unsigned, MaxVertexLights>;

    /// Construct.
    SceneBatchCollector(Context* context);
    /// Destruct.
    ~SceneBatchCollector();

    /// Set max number of pixel lights per drawable. Important lights may override this limit.
    void SetMaxPixelLights(unsigned count) { maxPixelLights_ = count; }

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
    Light* GetMainLight() const { return mainLightIndex_ != M_MAX_UNSIGNED ? visibleLights_[mainLightIndex_] : nullptr; }
    /// Return base batches for given pass.
    const ea::vector<SceneBatch>& GetBaseBatches(const ea::string& pass) const;
    /// Return sorted base batches for given pass.
    template <class T> void GetSortedBaseBatches(const ea::string& pass, ea::vector<T>& sortedBatches) const;
    /// Return light batches for given pass.
    const ThreadedVector<LightSceneBatch>& GetLightBatches(const ea::string& pass) const;
    /// Return sorted light batches for given pass.
    template <class T> void GetSortedLightBatches(const ea::string& pass, ea::vector<T>& sortedBatches) const;

    /// Return vertex lights for drawable (as indices in the array of visible lights).
    VertexLightCollection GetVertexLightIndices(unsigned drawableIndex) const { return drawableLighting_[drawableIndex].GetVertexLights(); }
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
    /// Find main light.
    unsigned FindMainLight() const;
    /// Process light in worker thread.
    void ProcessLightThreaded(Light* light, LightData& lightData);
    /// Collect lit geometries.
    void CollectLitGeometries(Light* light, LightData& lightData);
    /// Accumulate forward lighting for given light.
    void AccumulateForwardLighting(unsigned lightIndex);

    /// Collect scene batches.
    void CollectSceneBatches();
    /// Convert scene batches from intermediate batches to unlit base batches.
    void CollectSceneUnlitBaseBatches(SubPassPipelineStateCache& subPassCache,
        const ThreadedVector<IntermediateSceneBatch>& intermediateBatches, ea::vector<SceneBatch>& sceneBatches);
    /// Convert scene batches from intermediate batches to lit base batches and light batches.
    void CollectSceneLitBaseBatches(SubPassPipelineStateCache& baseSubPassCache, SubPassPipelineStateCache& lightSubPassCache,
        const ThreadedVector<IntermediateSceneBatch>& intermediateBatches, ea::vector<SceneBatch>& baseSceneBatches,
        ThreadedVector<LightSceneBatch>& lightSceneBatches);

    /// Max number of pixel lights per drawable. Important lights may override this limit.
    unsigned maxPixelLights_{ 1 };

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
    /// Light batches lookup table.
    ea::unordered_map<unsigned, ThreadedVector<LightSceneBatch>*> lightBatchesLookup_;

    /// Visible geometries.
    ThreadedVector<Drawable*> visibleGeometries_;
    /// Temporary thread-safe collection of visible lights.
    ThreadedVector<Light*> visibleLightsTemp_;
    /// Visible lights.
    ea::vector<Light*> visibleLights_;
    /// Index of main directional light in visible lights collection.
    unsigned mainLightIndex_{ M_MAX_UNSIGNED };
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

    /// Temporary collection for pipeline state cache misses (base batches).
    ThreadedVector<SceneBatch*> baseSceneBatchesWithoutPipelineStates_;
    /// Temporary collection for pipeline state cache misses (light batches).
    ThreadedVector<unsigned> lightSceneBatchesWithoutPipelineStates_;
};

template <class T>
void SceneBatchCollector::GetSortedBaseBatches(const ea::string& pass, ea::vector<T>& sortedBatches) const
{
    const ea::vector<SceneBatch>& baseBatches = GetBaseBatches(pass);
    const unsigned numBatches = baseBatches.size();
    sortedBatches.resize(numBatches);
    for (unsigned i = 0; i < numBatches; ++i)
        sortedBatches[i] = T{ &baseBatches[i] };
    ea::sort(sortedBatches.begin(), sortedBatches.end());
}

template <class T>
void SceneBatchCollector::GetSortedLightBatches(const ea::string& pass, ea::vector<T>& sortedBatches) const
{
    const ThreadedVector<LightSceneBatch>& lightBatches = GetLightBatches(pass);
    const unsigned numBatches = lightBatches.Size();
    sortedBatches.resize(numBatches);
    lightBatches.ForEach([&](unsigned, unsigned elementIndex, const LightSceneBatch& lightBatch)
    {
        sortedBatches[elementIndex] = T{ &lightBatch };
    });
    ea::sort(sortedBatches.begin(), sortedBatches.end());
}

}
