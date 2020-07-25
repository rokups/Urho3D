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
#include "../Graphics/DrawableLightAccumulator.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Light.h"
#include "../Graphics/Material.h"
#include "../Graphics/Technique.h"
#include "../Graphics/SceneBatch.h"
#include "../Graphics/SceneDrawableData.h"
#include "../Graphics/SceneLight.h"
#include "../Math/NumericRange.h"
#include "../Math/SphericalHarmonics.h"

#include <EASTL/fixed_vector.h>
#include <EASTL/sort.h>
#include <EASTL/vector_map.h>

namespace Urho3D
{

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

    /// Return main light index.
    unsigned GetMainLightIndex() const { return mainLightIndex_; }
    /// Return main light.
    Light* GetMainLight() const { return mainLightIndex_ != M_MAX_UNSIGNED ? visibleLights_[mainLightIndex_]->GetLight() : nullptr; }
    /// Return visible light by index.
    const SceneLight* GetVisibleLight(unsigned i) const { return visibleLights_[i]; }
    /// Return all visible lights.
    const ea::vector<SceneLight*>& GetVisibleLights() const { return visibleLights_; }
    /// Return base batches for given pass.
    const ea::vector<BaseSceneBatch>& GetBaseBatches(const ea::string& pass) const;
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
    /// Accumulate forward lighting for given light.
    void AccumulateForwardLighting(unsigned lightIndex);

    /// Collect scene batches.
    void CollectSceneBatches();
    /// Convert scene batches from intermediate batches to unlit base batches.
    void CollectSceneUnlitBaseBatches(SubPassPipelineStateCache& subPassCache,
        const ThreadedVector<IntermediateSceneBatch>& intermediateBatches, ea::vector<BaseSceneBatch>& sceneBatches);
    /// Convert scene batches from intermediate batches to lit base batches and light batches.
    void CollectSceneLitBaseBatches(SubPassPipelineStateCache& baseSubPassCache, SubPassPipelineStateCache& lightSubPassCache,
        const ThreadedVector<IntermediateSceneBatch>& intermediateBatches, ea::vector<BaseSceneBatch>& baseSceneBatches,
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
    ea::unordered_map<unsigned, ea::vector<BaseSceneBatch>*> baseBatchesLookup_;
    /// Light batches lookup table.
    ea::unordered_map<unsigned, ThreadedVector<LightSceneBatch>*> lightBatchesLookup_;

    /// Visible geometries.
    ThreadedVector<Drawable*> visibleGeometries_;
    /// Temporary thread-safe collection of visible lights.
    ThreadedVector<Light*> visibleLightsTemp_;
    /// Visible lights.
    ea::vector<SceneLight*> visibleLights_;
    /// Index of main directional light in visible lights collection.
    unsigned mainLightIndex_{ M_MAX_UNSIGNED };
    /// Scene Z range.
    SceneZRange sceneZRange_;

    /// Common drawable data index.
    SceneDrawableData transient_;
    /// Drawable lighting data index.
    ea::vector<DrawableLightAccumulator<MaxPixelLights, MaxVertexLights>> drawableLighting_;

    /// Per-light caches.
    ea::unordered_map<WeakPtr<Light>, ea::unique_ptr<SceneLight>> cachedSceneLights_;

    /// Temporary collection for pipeline state cache misses (base batches).
    ThreadedVector<BaseSceneBatch*> baseSceneBatchesWithoutPipelineStates_;
    /// Temporary collection for pipeline state cache misses (light batches).
    ThreadedVector<unsigned> lightSceneBatchesWithoutPipelineStates_;
};

template <class T>
void SceneBatchCollector::GetSortedBaseBatches(const ea::string& pass, ea::vector<T>& sortedBatches) const
{
    const ea::vector<BaseSceneBatch>& baseBatches = GetBaseBatches(pass);
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
