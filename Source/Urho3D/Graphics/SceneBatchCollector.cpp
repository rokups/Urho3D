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

#include "../Precompiled.h"

#include "../Core/Context.h"
#include "../Core/IteratorRange.h"
#include "../Core/WorkQueue.h"
#include "../Graphics/Camera.h"
#include "../Graphics/Geometry.h"
#include "../Graphics/Octree.h"
#include "../Graphics/OctreeQuery.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/SceneBatchCollector.h"
#include "../Scene/Scene.h"

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

/// Frustum Query for point light.
struct PointLightLitGeometriesQuery : public SphereOctreeQuery
{
    /// Return light sphere for the query.
    static Sphere GetLightSphere(Light* light)
    {
        return Sphere(light->GetNode()->GetWorldPosition(), light->GetRange());
    }

    /// Construct.
    PointLightLitGeometriesQuery(ea::vector<Drawable*>& result,
        const SceneDrawableData& transientData, Light* light)
        : SphereOctreeQuery(result, GetLightSphere(light), DRAWABLE_GEOMETRY)
        , transientData_(&transientData)
        , lightMask_(light->GetLightMaskEffective())
    {
    }

    void TestDrawables(Drawable** start, Drawable** end, bool inside) override
    {
        for (Drawable* drawable : MakeIteratorRange(start, end))
        {
            const unsigned drawableIndex = drawable->GetDrawableIndex();
            const unsigned traits = transientData_->traits_[drawableIndex];
            if (traits & SceneDrawableData::DrawableVisibleGeometry)
            {
                if (drawable->GetLightMask() & lightMask_)
                {
                    if (inside || sphere_.IsInsideFast(drawable->GetWorldBoundingBox()))
                        result_.push_back(drawable);
                }
            }
        }
    }

    /// Visiblity cache.
    const SceneDrawableData* transientData_{};
    /// Light mask to check.
    unsigned lightMask_{};
};

/// Frustum Query for spot light.
struct SpotLightLitGeometriesQuery : public FrustumOctreeQuery
{
    /// Construct.
    SpotLightLitGeometriesQuery(ea::vector<Drawable*>& result,
        const SceneDrawableData& transientData, Light* light)
        : FrustumOctreeQuery(result, light->GetFrustum(), DRAWABLE_GEOMETRY)
        , transientData_(&transientData)
        , lightMask_(light->GetLightMaskEffective())
    {
    }

    void TestDrawables(Drawable** start, Drawable** end, bool inside) override
    {
        for (Drawable* drawable : MakeIteratorRange(start, end))
        {
            const unsigned drawableIndex = drawable->GetDrawableIndex();
            const unsigned traits = transientData_->traits_[drawableIndex];
            if (traits & SceneDrawableData::DrawableVisibleGeometry)
            {
                if (drawable->GetLightMask() & lightMask_)
                {
                    if (inside || frustum_.IsInsideFast(drawable->GetWorldBoundingBox()))
                        result_.push_back(drawable);
                }
            }
        }
    }

    /// Visiblity cache.
    const SceneDrawableData* transientData_{};
    /// Light mask to check.
    unsigned lightMask_{};
};

/// Return light parameters important for pipeline state.
unsigned GetLightPipelineStateHash(Light* light, bool hasShadow)
{
    unsigned hash = 0;
    hash |= light->GetLightType() & 0x3;
    hash |= static_cast<unsigned>(hasShadow) << 2;
    hash |= static_cast<unsigned>(!!light->GetShapeTexture()) << 3;
    hash |= static_cast<unsigned>(light->GetSpecularIntensity() > 0.0f) << 4;
    hash |= static_cast<unsigned>(light->GetShadowBias().normalOffset_ > 0.0f) << 5;
    return hash;
}

}

struct SceneBatchCollector::IntermediateSceneBatch
{
    /// Geometry.
    Drawable* geometry_{};
    /// Index of source batch within geometry.
    unsigned sourceBatchIndex_{};
    /// Base material pass.
    Pass* basePass_{};
    /// Additional material pass for forward rendering.
    Pass* additionalPass_{};
};

struct SceneBatchCollector::SubPassPipelineStateKey
{
    /// Internal state of drawable that affects pipeline state.
    unsigned drawableHash_{};
    /// Lighting configuration.
    unsigned lightHash_{};
    /// Geometry type.
    GeometryType geometryType_{};
    /// Geometry to be rendered.
    Geometry* geometry_{};
    /// Material to be rendered.
    Material* material_{};
    /// Pass of the material technique to be used.
    Pass* pass_{};

    /// Construct default.
    SubPassPipelineStateKey() = default;

    /// Construct from base, litbase or light batch.
    SubPassPipelineStateKey(const BaseSceneBatch& sceneBatch, unsigned lightHash)
        : drawableHash_(sceneBatch.drawable_->GetPipelineStateHash())
        , lightHash_(lightHash)
        , geometryType_(sceneBatch.geometryType_)
        , geometry_(sceneBatch.geometry_)
        , material_(sceneBatch.material_)
        , pass_(sceneBatch.pass_)
    {
    }

    /// Compare.
    bool operator ==(const SubPassPipelineStateKey& rhs) const
    {
        return drawableHash_ == rhs.drawableHash_
            && lightHash_ == rhs.lightHash_
            && geometryType_ == rhs.geometryType_
            && geometry_ == rhs.geometry_
            && material_ == rhs.material_
            && pass_ == rhs.pass_;
    }

    /// Return hash.
    unsigned ToHash() const
    {
        unsigned hash = 0;
        CombineHash(hash, MakeHash(drawableHash_));
        CombineHash(hash, MakeHash(lightHash_));
        CombineHash(hash, MakeHash(geometryType_));
        CombineHash(hash, MakeHash(geometry_));
        CombineHash(hash, MakeHash(material_));
        CombineHash(hash, MakeHash(pass_));
        return hash;
    }
};

struct SceneBatchCollector::SubPassPipelineStateEntry
{
    /// Cached state of the geometry.
    unsigned geometryHash_{};
    /// Cached state of the material.
    unsigned materialHash_{};
    /// Cached state of the pass.
    unsigned passHash_{};

    /// Pipeline state.
    SharedPtr<PipelineState> pipelineState_;
    /// Whether the state is invalidated.
    mutable std::atomic_bool invalidated_;
};

struct SceneBatchCollector::SubPassPipelineStateContext
{
    /// Cull camera.
    Camera* camera_{};
    /// Light.
    Light* light_{};
    /// Whether the light has shadows.
    bool shadowed_{};
};

struct SceneBatchCollector::SubPassPipelineStateCache
{
public:
    /// Return existing pipeline state. Thread-safe.
    PipelineState* GetPipelineState(const SubPassPipelineStateKey& key) const
    {
        const auto iter = cache_.find(key);
        if (iter == cache_.end() || iter->second.invalidated_.load(std::memory_order_relaxed))
            return nullptr;

        const SubPassPipelineStateEntry& entry = iter->second;
        if (key.geometry_->GetPipelineStateHash() != entry.geometryHash_
            || key.material_->GetPipelineStateHash() != entry.materialHash_
            || key.pass_->GetPipelineStateHash() != entry.passHash_)
        {
            entry.invalidated_.store(true, std::memory_order_relaxed);
            return nullptr;
        }

        return entry.pipelineState_;
    }

    /// Return existing or create new pipeline state. Not thread safe.
    PipelineState* GetOrCreatePipelineState(Drawable* drawable, const SubPassPipelineStateKey& key,
        SubPassPipelineStateContext& factoryContext, ScenePipelineStateFactory& factory)
    {
        SubPassPipelineStateEntry& entry = cache_[key];
        if (!entry.pipelineState_ || entry.invalidated_.load(std::memory_order_relaxed)
            || key.geometry_->GetPipelineStateHash() != entry.geometryHash_
            || key.material_->GetPipelineStateHash() != entry.materialHash_
            || key.pass_->GetPipelineStateHash() != entry.passHash_)
        {
            entry.pipelineState_ = factory.CreatePipelineState(factoryContext.camera_, drawable,
                key.geometry_, key.geometryType_, key.material_, key.pass_, factoryContext.light_);
            entry.geometryHash_ = key.geometry_->GetPipelineStateHash();
            entry.materialHash_ = key.material_->GetPipelineStateHash();
            entry.passHash_ = key.pass_->GetPipelineStateHash();
            entry.invalidated_.store(false, std::memory_order_relaxed);
        }

        return entry.pipelineState_;
    }

private:
    /// Cached states, possibly invalid.
    ea::unordered_map<SubPassPipelineStateKey, SubPassPipelineStateEntry> cache_;
};

struct SceneBatchCollector::PassData
{
    /// Pass description.
    ScenePassDescription desc_;
    /// Base pass index.
    unsigned unlitBasePassIndex_{};
    /// First light pass index.
    unsigned litBasePassIndex_{};
    /// Additional light pass index.
    unsigned additionalLightPassIndex_{};

    /// Unlit intermediate batches.
    ThreadedVector<IntermediateSceneBatch> unlitBatches_;
    /// Lit intermediate batches. Always empty for Unlit passes.
    ThreadedVector<IntermediateSceneBatch> litBatches_;

    /// Unlit base scene batches.
    ea::vector<BaseSceneBatch> unlitBaseSceneBatches_;
    /// Lit base scene batches.
    ea::vector<BaseSceneBatch> litBaseSceneBatches_;
    /// Additional forward light batches.
    ThreadedVector<LightSceneBatch> additionalLightSceneBatches_;

    /// Pipeline state cache for unlit batches.
    SubPassPipelineStateCache unlitPipelineStateCache_;
    /// Pipeline state cache for lit batches.
    SubPassPipelineStateCache litPipelineStateCache_;
    /// Pipeline state cache for additional light batches.
    SubPassPipelineStateCache additionalLightPipelineStateCache_;

    /// Return whether given subpasses are present.
    bool CheckSubPasses(bool hasBase, bool hasFirstLight, bool hasAdditionalLight) const
    {
        return (unlitBasePassIndex_ != M_MAX_UNSIGNED) == hasBase
            && (litBasePassIndex_ != M_MAX_UNSIGNED) == hasFirstLight
            && (additionalLightPassIndex_ != M_MAX_UNSIGNED) == hasAdditionalLight;
    }

    /// Return whether is valid.
    bool IsValid() const
    {
        switch (desc_.type_)
        {
        case ScenePassType::Unlit:
            return CheckSubPasses(true, false, false);
        case ScenePassType::ForwardLitBase:
            return CheckSubPasses(false, true, true) || CheckSubPasses(true, true, true);
        case ScenePassType::ForwardUnlitBase:
            return CheckSubPasses(true, false, true);
        default:
            return false;
        }
    }

    /// Create intermediate scene batch. Batch is not added to any queue.
    IntermediateSceneBatch CreateIntermediateSceneBatch(Drawable* geometry, unsigned sourceBatchIndex,
        Pass* unlitBasePass, Pass* litBasePass, Pass* additionalLightPass) const
    {
        if (desc_.type_ == ScenePassType::Unlit || !additionalLightPass)
            return { geometry, sourceBatchIndex, unlitBasePass, nullptr };
        else if (desc_.type_ == ScenePassType::ForwardUnlitBase && unlitBasePass && additionalLightPass)
            return { geometry, sourceBatchIndex, unlitBasePass, additionalLightPass };
        else if (desc_.type_ == ScenePassType::ForwardLitBase && litBasePass && additionalLightPass)
            return { geometry, sourceBatchIndex, litBasePass, additionalLightPass };
        else
            return {};
    }

    /// Clear state before rendering.
    void Clear(unsigned numThreads)
    {
        unlitBatches_.Clear(numThreads);
        litBatches_.Clear(numThreads);
    }
};

struct SceneBatchCollector::DrawableZRangeEvaluator
{
    explicit DrawableZRangeEvaluator(Camera* camera)
        : viewMatrix_(camera->GetView())
        , viewZ_(viewMatrix_.m20_, viewMatrix_.m21_, viewMatrix_.m22_)
        , absViewZ_(viewZ_.Abs())
    {
    }

    DrawableZRange Evaluate(Drawable* drawable) const
    {
        const BoundingBox& boundingBox = drawable->GetWorldBoundingBox();
        const Vector3 center = boundingBox.Center();
        const Vector3 edge = boundingBox.Size() * 0.5f;

        // Ignore "infinite" objects like skybox
        if (edge.LengthSquared() >= M_LARGE_VALUE * M_LARGE_VALUE)
            return {};

        const float viewCenterZ = viewZ_.DotProduct(center) + viewMatrix_.m23_;
        const float viewEdgeZ = absViewZ_.DotProduct(edge);
        const float minZ = viewCenterZ - viewEdgeZ;
        const float maxZ = viewCenterZ + viewEdgeZ;

        return { minZ, maxZ };
    }

    Matrix3x4 viewMatrix_;
    Vector3 viewZ_;
    Vector3 absViewZ_;
};


SceneBatchCollector::SceneBatchCollector(Context* context)
    : Object(context)
    , workQueue_(context->GetWorkQueue())
    , renderer_(context->GetRenderer())
{}

SceneBatchCollector::~SceneBatchCollector()
{
}

const ea::vector<BaseSceneBatch>& SceneBatchCollector::GetBaseBatches(const ea::string& pass) const
{
    // TODO: Do we need to optimize it?
    const unsigned passIndex = Technique::GetPassIndex(pass);
    const auto baseBatchesIter = baseBatchesLookup_.find(passIndex);

    static const ea::vector<BaseSceneBatch> noBatches;
    if (baseBatchesIter == baseBatchesLookup_.end())
        return noBatches;

    return *baseBatchesIter->second;
}

const ThreadedVector<LightSceneBatch>& SceneBatchCollector::GetLightBatches(const ea::string& pass) const
{
    // TODO: Do we need to optimize it?
    const unsigned passIndex = Technique::GetPassIndex(pass);
    const auto baseBatchesIter = lightBatchesLookup_.find(passIndex);

    static const ThreadedVector<LightSceneBatch> noBatches;
    if (baseBatchesIter == lightBatchesLookup_.end())
        return noBatches;

    return *baseBatchesIter->second;
}

ea::array<Light*, SceneBatchCollector::MaxVertexLights> SceneBatchCollector::GetVertexLights(unsigned drawableIndex) const
{
    const auto indices = GetVertexLightIndices(drawableIndex);
    ea::array<Light*, MaxVertexLights> lights;
    for (unsigned i = 0; i < MaxVertexLights; ++i)
        lights[i] = indices[i] != M_MAX_UNSIGNED ? visibleLights_[indices[i]]->light_ : nullptr;
    return lights;
}

Technique* SceneBatchCollector::FindTechnique(Drawable* drawable, Material* material) const
{
    const ea::vector<TechniqueEntry>& techniques = material->GetTechniques();

    // If only one technique, no choice
    if (techniques.size() == 1)
        return techniques[0].technique_;

    // TODO: Consider optimizing this loop
    const float lodDistance = drawable->GetLodDistance();
    for (unsigned i = 0; i < techniques.size(); ++i)
    {
        const TechniqueEntry& entry = techniques[i];
        Technique* tech = entry.technique_;

        if (!tech || (!tech->IsSupported()) || materialQuality_ < entry.qualityLevel_)
            continue;
        if (lodDistance >= entry.lodDistance_)
            return tech;
    }

    // If no suitable technique found, fallback to the last
    return techniques.size() ? techniques.back().technique_ : nullptr;
}

void SceneBatchCollector::InitializeFrame(const FrameInfo& frameInfo, ScenePipelineStateFactory& pipelineStateFactory)
{
    numThreads_ = workQueue_->GetNumThreads() + 1;
    pipelineStateFactory_ = &pipelineStateFactory;
    materialQuality_ = renderer_->GetMaterialQuality();

    frameInfo_ = frameInfo;
    octree_ = frameInfo.octree_;
    camera_ = frameInfo.camera_;
    numDrawables_ = octree_->GetAllDrawables().size();

    if (camera_->GetViewOverrideFlags() & VO_LOW_MATERIAL_QUALITY)
        materialQuality_ = QUALITY_LOW;

    visibleGeometries_.Clear(numThreads_);
    visibleLightsTemp_.Clear(numThreads_);
    sceneZRange_.Clear(numThreads_);

    transient_.Reset(numDrawables_);
    drawableLighting_.resize(numDrawables_);
}

void SceneBatchCollector::InitializePasses(ea::span<const ScenePassDescription> passes)
{
    const unsigned numPasses = passes.size();
    passes_.resize(numPasses);
    for (unsigned i = 0; i < numPasses; ++i)
    {
        PassData& passData = passes_[i];
        passData.desc_ = passes[i];

        passData.unlitBasePassIndex_ = Technique::GetPassIndex(passData.desc_.unlitBasePassName_);
        passData.litBasePassIndex_ = Technique::GetPassIndex(passData.desc_.litBasePassName_);
        passData.additionalLightPassIndex_ = Technique::GetPassIndex(passData.desc_.additionalLightPassName_);

        if (!passData.IsValid())
        {
            // TODO: Log error
            assert(0);
            continue;
        }

        passData.Clear(numThreads_);
    }

    baseBatchesLookup_.clear();
    lightBatchesLookup_.clear();
    for (PassData& passData : passes_)
    {
        if (passData.unlitBasePassIndex_ != M_MAX_UNSIGNED)
            baseBatchesLookup_[passData.unlitBasePassIndex_] = &passData.unlitBaseSceneBatches_;
        if (passData.litBasePassIndex_ != M_MAX_UNSIGNED)
            baseBatchesLookup_[passData.litBasePassIndex_] = &passData.litBaseSceneBatches_;
        if (passData.additionalLightPassIndex_ != M_MAX_UNSIGNED)
            lightBatchesLookup_[passData.additionalLightPassIndex_] = &passData.additionalLightSceneBatches_;
    }
}

void SceneBatchCollector::UpdateAndCollectSourceBatches(const ea::vector<Drawable*>& drawables)
{
    ForEachParallel(workQueue_, drawableWorkThreshold_, drawables,
        [this](unsigned threadIndex, unsigned /*offset*/, ea::span<Drawable* const> drawablesRange)
    {
        UpdateAndCollectSourceBatchesForThread(threadIndex, drawablesRange);
    });
}

void SceneBatchCollector::UpdateAndCollectSourceBatchesForThread(unsigned threadIndex, ea::span<Drawable* const> drawables)
{
    Material* defaultMaterial = renderer_->GetDefaultMaterial();
    const DrawableZRangeEvaluator zRangeEvaluator{ camera_ };

    for (Drawable* drawable : drawables)
    {
        // TODO: Add occlusion culling
        const unsigned drawableIndex = drawable->GetDrawableIndex();

        drawable->UpdateBatches(frameInfo_);
        transient_.traits_[drawableIndex] |= SceneDrawableData::DrawableUpdated;

        // Skip if too far
        const float maxDistance = drawable->GetDrawDistance();
        if (maxDistance > 0.0f)
        {
            if (drawable->GetDistance() > maxDistance)
                return;
        }

        // For geometries, find zone, clear lights and calculate view space Z range
        if (drawable->GetDrawableFlags() & DRAWABLE_GEOMETRY)
        {
            const DrawableZRange zRange = zRangeEvaluator.Evaluate(drawable);

            // Do not add "infinite" objects like skybox to prevent shadow map focusing behaving erroneously
            if (!zRange.IsValid())
                transient_.zRange_[drawableIndex] = { M_LARGE_VALUE, M_LARGE_VALUE };
            else
            {
                transient_.zRange_[drawableIndex] = zRange;
                sceneZRange_.Accumulate(threadIndex, zRange);
            }

            visibleGeometries_.Insert(threadIndex, drawable);
            transient_.traits_[drawableIndex] |= SceneDrawableData::DrawableVisibleGeometry;

            // Collect batches
            const auto& sourceBatches = drawable->GetBatches();
            for (unsigned i = 0; i < sourceBatches.size(); ++i)
            {
                const SourceBatch& sourceBatch = sourceBatches[i];

                // Find current technique
                Material* material = sourceBatch.material_ ? sourceBatch.material_ : defaultMaterial;
                Technique* technique = FindTechnique(drawable, material);
                if (!technique)
                    continue;

                // Fill passes
                for (PassData& pass : passes_)
                {
                    Pass* unlitBasePass = technique->GetPass(pass.unlitBasePassIndex_);
                    Pass* litBasePass = technique->GetPass(pass.litBasePassIndex_);
                    Pass* additionalLightPass = technique->GetPass(pass.additionalLightPassIndex_);

                    const IntermediateSceneBatch sceneBatch = pass.CreateIntermediateSceneBatch(
                        drawable, i, unlitBasePass, litBasePass, additionalLightPass);

                    if (sceneBatch.additionalPass_)
                    {
                        transient_.traits_[drawableIndex] |= SceneDrawableData::ForwardLit;
                        pass.litBatches_.Insert(threadIndex, sceneBatch);
                    }
                    else if (sceneBatch.basePass_)
                        pass.unlitBatches_.Insert(threadIndex, sceneBatch);
                }
            }

            // Reset light accumulator
            // TODO: Don't do it if unlit
            drawableLighting_[drawableIndex].Reset();
        }
        else if (drawable->GetDrawableFlags() & DRAWABLE_LIGHT)
        {
            auto light = static_cast<Light*>(drawable);
            const Color lightColor = light->GetEffectiveColor();

            // Skip lights with zero brightness or black color, skip baked lights too
            if (!lightColor.Equals(Color::BLACK) && light->GetLightMaskEffective() != 0)
                visibleLightsTemp_.Insert(threadIndex, light);
        }
    }
}

void SceneBatchCollector::ProcessVisibleLights()
{
    // Allocate or clear scene lights
    visibleLights_.clear();
    visibleLightsTemp_.ForEach([&](unsigned, unsigned, Light* light)
    {
        WeakPtr<Light> weakLight(light);
        auto& sceneLight = cachedSceneLights_[weakLight];
        if (!sceneLight)
            sceneLight = ea::make_unique<SceneLight>(light);
        sceneLight->Clear();
        visibleLights_.push_back(sceneLight.get());
    });

    // Find main light
    mainLightIndex_ = FindMainLight();

    // Process lights in main thread
    for (SceneLight* sceneLight : visibleLights_)
    {
        sceneLight->pipelineStateHash_ = GetLightPipelineStateHash(sceneLight->light_, false);
    };

    // Process lights in worker threads
    for (unsigned i = 0; i < visibleLights_.size(); ++i)
    {
        workQueue_->AddWorkItem([this, i](unsigned threadIndex)
        {
            ProcessLightThreaded(*visibleLights_[i]);
        }, M_MAX_UNSIGNED);
        workQueue_->Complete(M_MAX_UNSIGNED);
    }

    // Accumulate lighting
    for (unsigned i = 0; i < visibleLights_.size(); ++i)
        AccumulateForwardLighting(i);
}

unsigned SceneBatchCollector::FindMainLight() const
{
    float mainLightScore = 0.0f;
    unsigned mainLightIndex = M_MAX_UNSIGNED;
    for (unsigned i = 0; i < visibleLights_.size(); ++i)
    {
        Light* light = visibleLights_[i]->light_;
        if (light->GetLightType() != LIGHT_DIRECTIONAL)
            continue;

        const float score = light->GetIntensityDivisor();
        if (score > mainLightScore)
        {
            mainLightScore = score;
            mainLightIndex = i;
        }
    }
    return mainLightIndex;
}

void SceneBatchCollector::ProcessLightThreaded(SceneLight& sceneLight)
{
    CollectLitGeometries(sceneLight);
}

void SceneBatchCollector::CollectLitGeometries(SceneLight& sceneLight)
{
    Light* light = sceneLight.light_;
    switch (light->GetLightType())
    {
    case LIGHT_SPOT:
    {
        SpotLightLitGeometriesQuery query(sceneLight.litGeometries_, transient_, light);
        octree_->GetDrawables(query);
        break;
    }
    case LIGHT_POINT:
    {
        PointLightLitGeometriesQuery query(sceneLight.litGeometries_, transient_, light);
        octree_->GetDrawables(query);
        break;
    }
    case LIGHT_DIRECTIONAL:
    {
        const unsigned lightMask = light->GetLightMask();
        visibleGeometries_.ForEach([&](unsigned, unsigned, Drawable* drawable)
        {
            if (drawable->GetLightMask() & lightMask)
                sceneLight.litGeometries_.push_back(drawable);
        });
        break;
    }
    }
}

void SceneBatchCollector::AccumulateForwardLighting(unsigned lightIndex)
{
    SceneLight& sceneLight = *visibleLights_[lightIndex];

    ForEachParallel(workQueue_, litGeometriesWorkThreshold_, sceneLight.litGeometries_,
        [&](unsigned /*threadIndex*/, unsigned /*offset*/, ea::span<Drawable* const> geometries)
    {
        DrawableLightDataAccumulationContext accumContext;
        accumContext.maxPixelLights_ = maxPixelLights_;
        accumContext.lightImportance_ = sceneLight.light_->GetLightImportance();
        accumContext.lightIndex_ = lightIndex;
        // TODO: fixme
        //accumContext.lights_ = &visibleLights_;

        const float lightIntensityPenalty = 1.0f / sceneLight.light_->GetIntensityDivisor();

        for (Drawable* geometry : geometries)
        {
            const unsigned drawableIndex = geometry->GetDrawableIndex();
            const float distance = ea::max(sceneLight.light_->GetDistanceTo(geometry), M_LARGE_EPSILON);
            const float penalty = lightIndex == mainLightIndex_ ? -M_LARGE_VALUE : distance * lightIntensityPenalty;
            drawableLighting_[drawableIndex].AccumulateLight(accumContext, penalty);
        }
    });
}

void SceneBatchCollector::CollectSceneBatches()
{
    for (PassData& passData : passes_)
    {
        CollectSceneUnlitBaseBatches(passData.unlitPipelineStateCache_,
            passData.unlitBatches_, passData.unlitBaseSceneBatches_);

        CollectSceneLitBaseBatches(passData.litPipelineStateCache_, passData.additionalLightPipelineStateCache_,
            passData.litBatches_, passData.litBaseSceneBatches_, passData.additionalLightSceneBatches_);
    }
}

void SceneBatchCollector::CollectSceneUnlitBaseBatches(SubPassPipelineStateCache& subPassCache,
    const ThreadedVector<IntermediateSceneBatch>& intermediateBatches, ea::vector<BaseSceneBatch>& sceneBatches)
{
    baseSceneBatchesWithoutPipelineStates_.Clear(numThreads_);
    sceneBatches.resize(intermediateBatches.Size());
    ForEachParallel(workQueue_, batchWorkThreshold_, intermediateBatches,
        [&](unsigned threadIndex, unsigned offset, ea::span<IntermediateSceneBatch const> batches)
    {
        Material* defaultMaterial = renderer_->GetDefaultMaterial();
        for (unsigned i = 0; i < batches.size(); ++i)
        {
            const IntermediateSceneBatch& intermediateBatch = batches[i];
            BaseSceneBatch& sceneBatch = sceneBatches[i + offset];

            Drawable* drawable = intermediateBatch.geometry_;
            const SourceBatch& sourceBatch = drawable->GetBatches()[intermediateBatch.sourceBatchIndex_];

            sceneBatch.drawable_ = drawable;
            sceneBatch.drawableIndex_ = drawable->GetDrawableIndex();
            sceneBatch.sourceBatchIndex_ = intermediateBatch.sourceBatchIndex_;
            sceneBatch.geometryType_ = sourceBatch.geometryType_;
            sceneBatch.geometry_ = sourceBatch.geometry_;
            sceneBatch.material_ = sourceBatch.material_ ? sourceBatch.material_ : defaultMaterial;
            sceneBatch.pass_ = intermediateBatch.basePass_;

            sceneBatch.pipelineState_ = subPassCache.GetPipelineState(SubPassPipelineStateKey{ sceneBatch, 0 });
            if (!sceneBatch.pipelineState_)
                baseSceneBatchesWithoutPipelineStates_.Insert(threadIndex, &sceneBatch);
        }
    });

    SubPassPipelineStateContext subPassContext;
    subPassContext.camera_ = camera_;
    subPassContext.light_ = nullptr;
    subPassContext.shadowed_ = false;

    baseSceneBatchesWithoutPipelineStates_.ForEach([&](unsigned, unsigned, BaseSceneBatch* sceneBatch)
    {
        const SubPassPipelineStateKey key{ *sceneBatch, 0 };
        sceneBatch->pipelineState_ = subPassCache.GetOrCreatePipelineState(
            sceneBatch->drawable_, key, subPassContext, *pipelineStateFactory_);
    });
}

void SceneBatchCollector::CollectSceneLitBaseBatches(
    SubPassPipelineStateCache& baseSubPassCache, SubPassPipelineStateCache& lightSubPassCache,
    const ThreadedVector<IntermediateSceneBatch>& intermediateBatches, ea::vector<BaseSceneBatch>& baseSceneBatches,
    ThreadedVector<LightSceneBatch>& lightSceneBatches)
{
    const unsigned baseLightHash = mainLightIndex_ != M_MAX_UNSIGNED
        ? visibleLights_[mainLightIndex_]->pipelineStateHash_
        : 0;

    baseSceneBatchesWithoutPipelineStates_.Clear(numThreads_);
    lightSceneBatchesWithoutPipelineStates_.Clear(numThreads_);

    baseSceneBatches.resize(intermediateBatches.Size());
    lightSceneBatches.Clear(numThreads_);

    ForEachParallel(workQueue_, batchWorkThreshold_, intermediateBatches,
        [&](unsigned threadIndex, unsigned offset, ea::span<IntermediateSceneBatch const> batches)
    {
        Material* defaultMaterial = renderer_->GetDefaultMaterial();
        for (unsigned i = 0; i < batches.size(); ++i)
        {
            const IntermediateSceneBatch& intermediateBatch = batches[i];
            BaseSceneBatch& sceneBatch = baseSceneBatches[i + offset];

            Drawable* drawable = intermediateBatch.geometry_;
            const SourceBatch& sourceBatch = drawable->GetBatches()[intermediateBatch.sourceBatchIndex_];

            sceneBatch.drawable_ = drawable;
            sceneBatch.drawableIndex_ = drawable->GetDrawableIndex();
            sceneBatch.sourceBatchIndex_ = intermediateBatch.sourceBatchIndex_;
            sceneBatch.geometryType_ = sourceBatch.geometryType_;
            sceneBatch.geometry_ = sourceBatch.geometry_;
            sceneBatch.material_ = sourceBatch.material_ ? sourceBatch.material_ : defaultMaterial;
            sceneBatch.pass_ = intermediateBatch.basePass_;

            const auto pixelLights = drawableLighting_[sceneBatch.drawableIndex_].GetPixelLights();
            const bool hasLitBase = !pixelLights.empty() && pixelLights[0].second == mainLightIndex_;

            // Add base batch
            const SubPassPipelineStateKey baseKey{ sceneBatch, hasLitBase ? baseLightHash : 0 };
            sceneBatch.pipelineState_ = baseSubPassCache.GetPipelineState(baseKey);
            if (!sceneBatch.pipelineState_)
                baseSceneBatchesWithoutPipelineStates_.Insert(threadIndex, &sceneBatch);

            // Add light batches
            for (unsigned j = hasLitBase ? 1 : 0; j < pixelLights.size(); ++j)
            {
                const unsigned additionalLightIndex = pixelLights[j].second;

                LightSceneBatch lightBatch;
                lightBatch.drawable_ = sceneBatch.drawable_;
                lightBatch.drawableIndex_ = sceneBatch.drawableIndex_;
                lightBatch.sourceBatchIndex_ = sceneBatch.sourceBatchIndex_;
                lightBatch.geometryType_ = sceneBatch.geometryType_;
                lightBatch.geometry_ = sceneBatch.geometry_;
                lightBatch.material_ = sceneBatch.material_;
                lightBatch.lightIndex_ = additionalLightIndex;
                lightBatch.pass_ = intermediateBatch.additionalPass_;

                const SubPassPipelineStateKey lightKey{ lightBatch, visibleLights_[additionalLightIndex]->pipelineStateHash_ };
                lightBatch.pipelineState_ = lightSubPassCache.GetPipelineState(lightKey);

                const unsigned batchIndex = lightSceneBatches.Insert(threadIndex, lightBatch);
                if (!lightBatch.pipelineState_)
                    lightSceneBatchesWithoutPipelineStates_.Insert(threadIndex, batchIndex);
            }
        }
    });

    // Resolve base pipeline states
    {
        SubPassPipelineStateContext baseSubPassContext;
        baseSubPassContext.camera_ = camera_;
        baseSubPassContext.light_ = mainLightIndex_ != M_MAX_UNSIGNED ? visibleLights_[mainLightIndex_]->light_ : nullptr;
        baseSubPassContext.shadowed_ = false;

        baseSceneBatchesWithoutPipelineStates_.ForEach([&](unsigned, unsigned, BaseSceneBatch* sceneBatch)
        {
            const SubPassPipelineStateKey baseKey{ *sceneBatch, baseLightHash };
            sceneBatch->pipelineState_ = baseSubPassCache.GetOrCreatePipelineState(
                sceneBatch->drawable_, baseKey, baseSubPassContext, *pipelineStateFactory_);
        });
    }

    // Resolve light pipeline states
    {
        SubPassPipelineStateContext lightSubPassContext;
        lightSubPassContext.camera_ = camera_;

        lightSceneBatchesWithoutPipelineStates_.ForEach([&](unsigned threadIndex, unsigned, unsigned batchIndex)
        {
            LightSceneBatch& lightBatch = lightSceneBatches.Get(threadIndex, batchIndex);
            SceneLight& sceneLight = *visibleLights_[lightBatch.lightIndex_];
            lightSubPassContext.light_ = sceneLight.light_;
            lightSubPassContext.shadowed_ = false;

            const SubPassPipelineStateKey lightKey{ lightBatch, sceneLight.pipelineStateHash_ };
            lightBatch.pipelineState_ = lightSubPassCache.GetOrCreatePipelineState(
                lightBatch.drawable_, lightKey, lightSubPassContext, *pipelineStateFactory_);
        });
    }
}

}
