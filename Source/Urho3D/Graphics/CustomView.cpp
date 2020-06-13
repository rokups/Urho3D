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
#include "../Graphics/Camera.h"
#include "../Graphics/ConstantBuffer.h"
#include "../Graphics/ConstantBufferLayout.h"
#include "../Graphics/CustomView.h"
#include "../Graphics/DrawCommandQueue.h"
#include "../Graphics/IndexBuffer.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Viewport.h"
#include "../Graphics/Detail/RenderingQueries.h"
#include "../Scene/Scene.h"

#include <EASTL/fixed_vector.h>
#include <EASTL/vector_map.h>
#include <EASTL/sort.h>

#include "../Graphics/Light.h"
#include "../Graphics/Octree.h"
#include "../Graphics/Geometry.h"
#include "../Graphics/Batch.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/Zone.h"
#include "../Graphics/PipelineState.h"
#include "../Core/WorkQueue.h"

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

/// Helper class to evaluate min and max Z of the drawable.
struct DrawableZRangeEvaluator
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

/// Light importance.
enum LightImportance
{
    LI_AUTO,
    LI_IMPORTANT,
    LI_NOT_IMPORTANT
};

/// Return light importance.
// TODO: Move it to Light
LightImportance GetLightImportance(Light* light)
{
    if (light->IsNegative())
        return LI_IMPORTANT;
    else if (light->GetPerVertex())
        return LI_NOT_IMPORTANT;
    else
        return LI_AUTO;
}

/// Return distance between light and geometry.
float GetLightGeometryDistance(Light* light, Drawable* geometry)
{
    if (light->GetLightType() == LIGHT_DIRECTIONAL)
        return 0.0f;

    const BoundingBox boundingBox = geometry->GetWorldBoundingBox();
    const Vector3 lightPosition = light->GetNode()->GetWorldPosition();
    const Vector3 minDelta = boundingBox.min_ - lightPosition;
    const Vector3 maxDelta = lightPosition - boundingBox.max_;
    return VectorMax(Vector3::ZERO, VectorMax(minDelta, maxDelta)).Length();
}

/// Return light penalty.
float GetLightPenalty(Light* light, Drawable* geometry)
{
    const float distance = GetLightGeometryDistance(light, geometry);
    const float intensity = 1.0f / light->GetIntensityDivisor();
    return distance * intensity;
}

/// Light accumulator context.
struct DrawableLightAccumulatorContext
{
    /// Max number of pixel lights.
    unsigned maxPixelLights_{ 1 };
};

/// Light accumulator.
/// MaxPixelLights: Max number of per-pixel lights supported. Important lights ignore this limit.
template <unsigned MaxPixelLights>
struct DrawableLightAccumulator
{
    /// Max number of per-vertex lights supported.
    static const unsigned MaxVertexLights = 4;
    /// Max number of lights that don't require allocations.
    static const unsigned NumElements = ea::max(MaxPixelLights + 1, 4u) + MaxVertexLights;
    /// Container for lights.
    using Container = ea::vector_map<float, Light*, ea::less<float>, ea::allocator,
        ea::fixed_vector<ea::pair<float, Light*>, NumElements>>;
    /// Container for vertex lights.
    using VertexLightContainer = ea::array<Light*, MaxVertexLights>;

    /// Reset accumulator.
    void Reset()
    {
        lights_.clear();
        numImportantLights_ = 0;
    }

    /// Accumulate light.
    void AccumulateLight(DrawableLightAccumulatorContext& ctx, float penalty, Light* light)
    {
        // Count important lights
        if (GetLightImportance(light) == LI_IMPORTANT)
        {
            penalty = -1.0f;
            ++numImportantLights_;
        }

        // Add new light
        lights_.emplace(penalty, light);

        // If too many lights, drop the least important one
        firstVertexLight_ = ea::max(ctx.maxPixelLights_, numImportantLights_);
        const unsigned maxLights = MaxVertexLights + firstVertexLight_;
        if (lights_.size() > maxLights)
        {
            // TODO: Update SH
            lights_.pop_back();
        }
    }

    /// Return main directional per-pixel light.
    Light* GetMainDirectionalLight() const
    {
        return lights_.empty() ? nullptr : lights_.front().second;
    }

    /// Return per-vertex lights.
    VertexLightContainer GetVertexLights() const
    {
        VertexLightContainer vertexLights{};
        for (unsigned i = firstVertexLight_; i < lights_.size(); ++i)
            vertexLights[i - firstVertexLight_] = lights_.at(i).second;
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

/// Process primary drawable.
void ProcessPrimaryDrawable(Drawable* drawable,
    const DrawableZRangeEvaluator& zRangeEvaluator,
    DrawableViewportCache& cache, unsigned threadIndex)
{
    const unsigned drawableIndex = drawable->GetDrawableIndex();
    cache.transient_.traits_[drawableIndex] |= TransientDrawableDataIndex::DrawableUpdated;

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
            cache.transient_.zRange_[drawableIndex] = { M_LARGE_VALUE, M_LARGE_VALUE };
        else
        {
            cache.transient_.zRange_[drawableIndex] = zRange;
            cache.sceneZRange_.Accumulate(threadIndex, zRange);
        }

        cache.visibleGeometries_.Insert(threadIndex, drawable);
        cache.transient_.traits_[drawableIndex] |= TransientDrawableDataIndex::DrawableVisibleGeometry;
    }
    else if (drawable->GetDrawableFlags() & DRAWABLE_LIGHT)
    {
        auto light = static_cast<Light*>(drawable);
        const Color lightColor = light->GetEffectiveColor();

        // Skip lights with zero brightness or black color, skip baked lights too
        if (!lightColor.Equals(Color::BLACK) && light->GetLightMaskEffective() != 0)
            cache.visibleLights_.Insert(threadIndex, light);
    }
}

Vector4 GetCameraDepthModeParameter(const Camera* camera)
{
    Vector4 depthMode = Vector4::ZERO;
    if (camera->IsOrthographic())
    {
        depthMode.x_ = 1.0f;
#ifdef URHO3D_OPENGL
        depthMode.z_ = 0.5f;
        depthMode.w_ = 0.5f;
#else
        depthMode.z_ = 1.0f;
#endif
    }
    else
        depthMode.w_ = 1.0f / camera->GetFarClip();
    return depthMode;
}

Vector4 GetCameraDepthReconstructParameter(const Camera* camera)
{
    const float nearClip = camera->GetNearClip();
    const float farClip = camera->GetFarClip();
    return {
        farClip / (farClip - nearClip),
        -nearClip / (farClip - nearClip),
        camera->IsOrthographic() ? 1.0f : 0.0f,
        camera->IsOrthographic() ? 0.0f : 1.0f
    };
}

Matrix4 GetEffectiveCameraViewProj(const Camera* camera)
{
    Matrix4 projection = camera->GetGPUProjection();
#ifdef URHO3D_OPENGL
    auto graphics = camera->GetSubsystem<Graphics>();
    // Add constant depth bias manually to the projection matrix due to glPolygonOffset() inconsistency
    const float constantBias = 2.0f * graphics->GetDepthConstantBias();
    projection.m22_ += projection.m32_ * constantBias;
    projection.m23_ += projection.m33_ * constantBias;
#endif
    return projection * camera->GetView();
}

Vector4 GetZoneFogParameter(const Zone* zone, const Camera* camera)
{
    const float farClip = camera->GetFarClip();
    float fogStart = Min(zone->GetFogStart(), farClip);
    float fogEnd = Min(zone->GetFogEnd(), farClip);
    if (fogStart >= fogEnd * (1.0f - M_LARGE_EPSILON))
        fogStart = fogEnd * (1.0f - M_LARGE_EPSILON);
    const float fogRange = Max(fogEnd - fogStart, M_EPSILON);
    return {
        fogEnd / farClip,
        farClip / fogRange,
        0.0f,
        0.0f
    };
}

void FillGlobalSharedParameters(DrawCommandQueue& drawQueue,
    const FrameInfo& frameInfo, const Camera* camera, const Zone* zone, const Scene* scene)
{
    if (drawQueue.BeginShaderParameterGroup(SP_FRAME))
    {
        drawQueue.AddShaderParameter(VSP_DELTATIME, frameInfo.timeStep_);
        drawQueue.AddShaderParameter(PSP_DELTATIME, frameInfo.timeStep_);

        const float elapsedTime = scene->GetElapsedTime();
        drawQueue.AddShaderParameter(VSP_ELAPSEDTIME, elapsedTime);
        drawQueue.AddShaderParameter(PSP_ELAPSEDTIME, elapsedTime);

        drawQueue.CommitShaderParameterGroup(SP_FRAME);
    }

    if (drawQueue.BeginShaderParameterGroup(SP_CAMERA))
    {
        const Matrix3x4 cameraEffectiveTransform = camera->GetEffectiveWorldTransform();
        drawQueue.AddShaderParameter(VSP_CAMERAPOS, cameraEffectiveTransform.Translation());
        drawQueue.AddShaderParameter(VSP_VIEWINV, cameraEffectiveTransform);
        drawQueue.AddShaderParameter(VSP_VIEW, camera->GetView());
        drawQueue.AddShaderParameter(PSP_CAMERAPOS, cameraEffectiveTransform.Translation());

        const float nearClip = camera->GetNearClip();
        const float farClip = camera->GetFarClip();
        drawQueue.AddShaderParameter(VSP_NEARCLIP, nearClip);
        drawQueue.AddShaderParameter(VSP_FARCLIP, farClip);
        drawQueue.AddShaderParameter(PSP_NEARCLIP, nearClip);
        drawQueue.AddShaderParameter(PSP_FARCLIP, farClip);

        drawQueue.AddShaderParameter(VSP_DEPTHMODE, GetCameraDepthModeParameter(camera));
        drawQueue.AddShaderParameter(PSP_DEPTHRECONSTRUCT, GetCameraDepthReconstructParameter(camera));

        Vector3 nearVector, farVector;
        camera->GetFrustumSize(nearVector, farVector);
        drawQueue.AddShaderParameter(VSP_FRUSTUMSIZE, farVector);

        drawQueue.AddShaderParameter(VSP_VIEWPROJ, GetEffectiveCameraViewProj(camera));

        drawQueue.CommitShaderParameterGroup(SP_CAMERA);
    }

    if (drawQueue.BeginShaderParameterGroup(SP_ZONE))
    {
        drawQueue.AddShaderParameter(VSP_AMBIENTSTARTCOLOR, Color::WHITE);
        drawQueue.AddShaderParameter(VSP_AMBIENTENDCOLOR, Vector4::ZERO);
        drawQueue.AddShaderParameter(VSP_ZONE, Matrix3x4::IDENTITY);
        drawQueue.AddShaderParameter(PSP_AMBIENTCOLOR, Color::WHITE);
        drawQueue.AddShaderParameter(PSP_FOGCOLOR, zone->GetFogColor());
        drawQueue.AddShaderParameter(PSP_FOGPARAMS, GetZoneFogParameter(zone, camera));

        drawQueue.CommitShaderParameterGroup(SP_ZONE);
    }
}

void ApplyShaderResources(Graphics* graphics, const ShaderResourceCollection& resources)
{
    for (const auto& item : resources)
    {
        if (graphics->HasTextureUnit(item.first))
            graphics->SetTexture(item.first, item.second);
    }
}

CullMode GetEffectiveCullMode(CullMode mode, const Camera* camera)
{
    // If a camera is specified, check whether it reverses culling due to vertical flipping or reflection
    if (camera && camera->GetReverseCulling())
    {
        if (mode == CULL_CW)
            mode = CULL_CCW;
        else if (mode == CULL_CCW)
            mode = CULL_CW;
    }

    return mode;
}

/// Pipeline state cache.
class PipelineStateCache : public Object
{
    URHO3D_OBJECT(PipelineStateCache, Object);

public:
    /// Construct.
    explicit PipelineStateCache(Context* context) : Object(context) {}
    /// Create new or return existing pipeline state.
    PipelineState* GetPipelineState(const PipelineStateDesc& desc)
    {
        SharedPtr<PipelineState>& state = states_[desc];
        if (!state)
        {
            state = MakeShared<PipelineState>(context_);
            state->Create(desc);
        }
        return state;
    }

private:
    /// Cached states.
    ea::unordered_map<PipelineStateDesc, SharedPtr<PipelineState>> states_;
};

struct BatchPipelineStateKey
{
    /// Geometry to be rendered.
    Geometry* geometry_{};
    /// Material to be rendered.
    Material* material_{};
    /// Pass of the material technique to be used.
    Pass* pass_{};
    /// Light to be applied.
    Light* light_{};

    /// Compare.
    bool operator ==(const BatchPipelineStateKey& rhs) const
    {
        return geometry_ == rhs.geometry_
            && material_ == rhs.material_
            && pass_ == rhs.pass_
            && light_ == rhs.light_;
    }

    /// Return hash.
    unsigned ToHash() const
    {
        unsigned hash = 0;
        CombineHash(hash, MakeHash(geometry_));
        CombineHash(hash, MakeHash(material_));
        CombineHash(hash, MakeHash(pass_));
        CombineHash(hash, MakeHash(light_));
        return hash;
    }
};

struct BatchPipelineState
{
    /// Pipeline state.
    SharedPtr<PipelineState> pipelineState_;
    /// Cached state of the geometry.
    unsigned geometryHash_{};
    /// Cached state of the material.
    unsigned materialHash_{};
    /// Cached state of the pass.
    unsigned passHash_{};
    // TODO: Hash light too
};

class BatchPipelineStateCache
{
public:
    BatchPipelineStateCache(PipelineStateCache& cache)
        : graphics_(cache.GetContext()->GetGraphics())
        , underlyingCache_(&cache)
    {}

    PipelineState* GetPipelineState(const BatchPipelineStateKey& key, Camera* camera)
    {
        Geometry* geometry = key.geometry_;
        Material* material = key.material_;
        Pass* pass = key.pass_;

        BatchPipelineState& state = validationMap_[key];
        if (!state.pipelineState_
            || geometry->GetPipelineStateHash() != state.geometryHash_
            || material->GetPipelineStateHash() != state.materialHash_
            || pass->GetPipelineStateHash() != state.passHash_)
        {
            PipelineStateDesc desc;

            for (VertexBuffer* vertexBuffer : geometry->GetVertexBuffers())
                desc.vertexElements_.append(vertexBuffer->GetElements());

            ea::string commonDefines = "DIRLIGHT NUMVERTEXLIGHTS=4 ";
            if (graphics_->GetConstantBuffersEnabled())
                commonDefines += "USE_CBUFFERS ";
            desc.vertexShader_ = graphics_->GetShader(
                VS, "v2/" + pass->GetVertexShader(), commonDefines + pass->GetEffectiveVertexShaderDefines());
            desc.pixelShader_ = graphics_->GetShader(
                PS, "v2/" + pass->GetPixelShader(), commonDefines + pass->GetEffectivePixelShaderDefines());

            desc.primitiveType_ = geometry->GetPrimitiveType();
            if (auto indexBuffer = geometry->GetIndexBuffer())
                desc.indexType_ = indexBuffer->GetIndexSize() == 2 ? IBT_UINT16 : IBT_UINT32;

            desc.depthWrite_ = true;
            desc.depthMode_ = CMP_LESSEQUAL;
            desc.stencilEnabled_ = false;
            desc.stencilMode_ = CMP_ALWAYS;

            desc.colorWrite_ = true;
            desc.blendMode_ = BLEND_REPLACE;
            desc.alphaToCoverage_ = false;

            desc.fillMode_ = FILL_SOLID;
            desc.cullMode_ = GetEffectiveCullMode(material->GetCullMode(), camera);

            state.pipelineState_ = underlyingCache_->GetPipelineState(desc);
            state.geometryHash_ = key.geometry_->GetPipelineStateHash();
            state.materialHash_ = key.material_->GetPipelineStateHash();
            state.passHash_ = key.pass_->GetPipelineStateHash();
        }
        return state.pipelineState_;
    }

private:
    Graphics* graphics_{};
    PipelineStateCache* underlyingCache_{};
    ea::unordered_map<BatchPipelineStateKey, BatchPipelineState> validationMap_;
};


struct ForwardBaseBatchSortKey
{
    /// Geometry to be rendered.
    Geometry* geometry_{};
    /// Material to be rendered.
    Material* material_{};
    /// Pipeline state.
    PipelineState* pipelineState_{};
    /// Hash of used shaders.
    unsigned shadersHash_{};
    /// Distance from camera.
    float distance_{};
    /// 8-bit render order modifier from material.
    unsigned char renderOrder_{};
};

struct ForwardBaseBatch : public ForwardBaseBatchSortKey
{
    /// Drawable to be rendered.
    Drawable* drawable_;
    /// Source batch of the drawable.
    const SourceBatch* sourceBatch_;
    /// Main per-pixel directional light.
    Light* mainDirectionalLight_{};
    /// Array of per-vertex lights.
    ea::array<Light*, 4> vertexLights_{};
    /// Accumulated SH lighting.
    SphericalHarmonicsDot9 shLighting_;
};

}

CustomView::CustomView(Context* context, CustomViewportScript* script)
    : Object(context)
    , graphics_(context_->GetGraphics())
    , workQueue_(context_->GetWorkQueue())
    , script_(script)
{}

CustomView::~CustomView()
{
}

bool CustomView::Define(RenderSurface* renderTarget, Viewport* viewport)
{
    scene_ = viewport->GetScene();
    camera_ = scene_ ? viewport->GetCamera() : nullptr;
    octree_ = scene_ ? scene_->GetComponent<Octree>() : nullptr;
    if (!camera_ || !octree_)
        return false;

    numDrawables_ = octree_->GetAllDrawables().size();
    renderTarget_ = renderTarget;
    viewport_ = viewport;
    return true;
}

void CustomView::Update(const FrameInfo& frameInfo)
{
    frameInfo_ = frameInfo;
    frameInfo_.camera_ = camera_;
    numThreads_ = workQueue_->GetNumThreads() + 1;
}

void CustomView::PostTask(std::function<void(unsigned)> task)
{
    workQueue_->AddWorkItem(ea::move(task), M_MAX_UNSIGNED);
}

void CustomView::CompleteTasks()
{
    workQueue_->Complete(M_MAX_UNSIGNED);
}

/*void CustomView::ClearViewport(ClearTargetFlags flags, const Color& color, float depth, unsigned stencil)
{
    graphics_->Clear(flags, color, depth, stencil);
}*/

void CustomView::CollectDrawables(DrawableCollection& drawables, Camera* camera, DrawableFlags flags)
{
    FrustumOctreeQuery query(drawables, camera->GetFrustum(), flags, camera->GetViewMask());
    octree_->GetDrawables(query);
}

void CustomView::ProcessPrimaryDrawables(DrawableViewportCache& viewportCache,
    const DrawableCollection& drawables, Camera* camera)
{
    // Reset cache
    viewportCache.visibleGeometries_.Clear(numThreads_);
    viewportCache.visibleLights_.Clear(numThreads_);
    viewportCache.sceneZRange_.Clear(numThreads_);
    viewportCache.transient_.Reset(numDrawables_);

    // Prepare frame info
    FrameInfo frameInfo = frameInfo_;
    frameInfo.camera_ = camera;

    // Process drawables
    const unsigned drawablesPerItem = (drawables.size() + numThreads_ - 1) / numThreads_;
    for (unsigned workItemIndex = 0; workItemIndex < numThreads_; ++workItemIndex)
    {
        const unsigned fromIndex = workItemIndex * drawablesPerItem;
        const unsigned toIndex = ea::min((workItemIndex + 1) * drawablesPerItem, drawables.size());

        workQueue_->AddWorkItem([&, camera, fromIndex, toIndex](unsigned threadIndex)
        {
            const DrawableZRangeEvaluator zRangeEvaluator{ camera };

            for (unsigned i = fromIndex; i < toIndex; ++i)
            {
                // TODO: Add occlusion culling
                Drawable* drawable = drawables[i];
                drawable->UpdateBatches(frameInfo);
                ProcessPrimaryDrawable(drawable, zRangeEvaluator, viewportCache, threadIndex);
            }
        }, M_MAX_UNSIGNED);
    }
    workQueue_->Complete(M_MAX_UNSIGNED);
}

void CustomView::CollectLitGeometries(const DrawableViewportCache& viewportCache,
    DrawableLightCache& lightCache, Light* light)
{
    switch (light->GetLightType())
    {
    case LIGHT_SPOT:
    {
        SpotLightLitGeometriesQuery query(lightCache.litGeometries_, viewportCache.transient_, light);
        octree_->GetDrawables(query);
        break;
    }
    case LIGHT_POINT:
    {
        PointLightLitGeometriesQuery query(lightCache.litGeometries_, viewportCache.transient_, light);
        octree_->GetDrawables(query);
        break;
    }
    case LIGHT_DIRECTIONAL:
    {
        const unsigned lightMask = light->GetLightMask();
        viewportCache.visibleGeometries_.ForEach([&](unsigned index, Drawable* drawable)
        {
            if (drawable->GetLightMask() & lightMask)
                lightCache.litGeometries_.push_back(drawable);
        });
        break;
    }
    }
}

void CustomView::Render()
{
    graphics_->SetRenderTarget(0, renderTarget_);
    graphics_->SetDepthStencil((RenderSurface*)nullptr);
    //graphics_->SetViewport(viewport_->GetRect() == IntRect::ZERO ? graphics_->GetViewport() : viewport_->GetRect());
    graphics_->Clear(CLEAR_COLOR | CLEAR_DEPTH | CLEAR_DEPTH, Color::RED * 0.5f);

    script_->Render(this);

    if (renderTarget_)
    {
        // On OpenGL, flip the projection if rendering to a texture so that the texture can be addressed in the same way
        // as a render texture produced on Direct3D9
#ifdef URHO3D_OPENGL
        if (camera_)
            camera_->SetFlipVertical(true);
#endif
    }

    // Set automatic aspect ratio if required
    if (camera_ && camera_->GetAutoAspectRatio())
        camera_->SetAspectRatioInternal((float)frameInfo_.viewSize_.x_ / (float)frameInfo_.viewSize_.y_);

    // Collect and process visible drawables
    static DrawableCollection drawablesInMainCamera;
    drawablesInMainCamera.clear();
    CollectDrawables(drawablesInMainCamera, camera_, DRAWABLE_GEOMETRY | DRAWABLE_LIGHT);

    static DrawableViewportCache viewportCache;
    ProcessPrimaryDrawables(viewportCache, drawablesInMainCamera, camera_);

    // Process visible lights
    static ea::vector<DrawableLightCache> globalLightCache;
    globalLightCache.clear();
    globalLightCache.resize(viewportCache.visibleLights_.Size());
    viewportCache.visibleLights_.ForEach([&](unsigned lightIndex, Light* light)
    {
        PostTask([&, this, light, lightIndex](unsigned threadIndex)
        {
            DrawableLightCache& lightCache = globalLightCache[lightIndex];
            CollectLitGeometries(viewportCache, lightCache, light);
        });
    });
    CompleteTasks();

    // Accumulate light
    static ea::vector<DrawableLightAccumulator<4>> lightAccumulator;
    lightAccumulator.clear();
    lightAccumulator.resize(numDrawables_);

    viewportCache.visibleLights_.ForEach([&](unsigned lightIndex, Light* light)
    {
        DrawableLightAccumulatorContext ctx;
        ctx.maxPixelLights_ = 1;

        DrawableLightCache& lightCache = globalLightCache[lightIndex];
        for (Drawable* litGeometry : lightCache.litGeometries_)
        {
            const unsigned drawableIndex = litGeometry->GetDrawableIndex();
            const float lightPenalty = GetLightPenalty(light, litGeometry);
            lightAccumulator[drawableIndex].AccumulateLight(ctx, lightPenalty, light);
        }
    });

    //
    static PipelineStateCache pipelineStateCache(context_);
    static BatchPipelineStateCache batchPipelineStateCache(pipelineStateCache);

    // Collect intermediate batches
    static ea::vector<ForwardBaseBatch> forwardBaseBatches;
    auto renderer = context_->GetRenderer();
    auto defaultMaterial = renderer->GetDefaultMaterial();
    forwardBaseBatches.clear();
    auto passIndex = Technique::GetPassIndex("litbase");
    viewportCache.visibleGeometries_.ForEach([&](unsigned index, Drawable* drawable)
    {
        const unsigned drawableIndex = drawable->GetDrawableIndex();
        const auto& drawableLights = lightAccumulator[drawableIndex];
        for (const SourceBatch& sourceBatch : drawable->GetBatches())
        {
            ForwardBaseBatch baseBatch;
            baseBatch.geometry_ = sourceBatch.geometry_;
            baseBatch.material_ = sourceBatch.material_ ? sourceBatch.material_ : defaultMaterial;
            baseBatch.distance_ = sourceBatch.distance_;
            baseBatch.renderOrder_ = baseBatch.material_->GetRenderOrder();

            baseBatch.drawable_ = drawable;
            baseBatch.sourceBatch_ = &sourceBatch;

            baseBatch.mainDirectionalLight_ = drawableLights.GetMainDirectionalLight();
            baseBatch.vertexLights_ = drawableLights.GetVertexLights();

            BatchPipelineStateKey pipelineStateKey;
            pipelineStateKey.geometry_ = baseBatch.geometry_;
            pipelineStateKey.material_ = baseBatch.material_;
            pipelineStateKey.pass_ = baseBatch.material_->GetTechnique(0)->GetSupportedPass(passIndex);
            pipelineStateKey.light_ = baseBatch.mainDirectionalLight_;
            if (!pipelineStateKey.pass_)
                continue;

            baseBatch.pipelineState_ = batchPipelineStateCache.GetPipelineState(pipelineStateKey, camera_);
            baseBatch.shadersHash_ = 0;

            forwardBaseBatches.push_back(baseBatch);
        }
    });

    // Collect batches
    static DrawCommandQueue drawQueue;
    drawQueue.Reset(graphics_);

    Material* currentMaterial = nullptr;
    bool first = true;
    const auto zone = octree_->GetZone();
    for (const ForwardBaseBatch& batch : forwardBaseBatches)
    {
        auto geometry = batch.geometry_;
        auto light = batch.mainDirectionalLight_;
        const SourceBatch& sourceBatch = *batch.sourceBatch_;
        drawQueue.SetPipelineState(batch.pipelineState_);
        FillGlobalSharedParameters(drawQueue, frameInfo_, camera_, zone, scene_);
        SphericalHarmonicsDot9 sh;
        if (batch.material_ != currentMaterial)
        {
            if (drawQueue.BeginShaderParameterGroup(SP_MATERIAL, true))
            {
            currentMaterial = batch.material_;
            const auto& parameters = batch.material_->GetShaderParameters();
            for (const auto& item : parameters)
                drawQueue.AddShaderParameter(item.first, item.second.value_);
            drawQueue.CommitShaderParameterGroup(SP_MATERIAL);
            }

            const auto& textures = batch.material_->GetTextures();
            for (const auto& item : textures)
                drawQueue.AddShaderResource(item.first, item.second);
            drawQueue.CommitShaderResources();
        }

        if (drawQueue.BeginShaderParameterGroup(SP_OBJECT, true))
        {
        drawQueue.AddShaderParameter(VSP_SHAR, sh.Ar_);
        drawQueue.AddShaderParameter(VSP_SHAG, sh.Ag_);
        drawQueue.AddShaderParameter(VSP_SHAB, sh.Ab_);
        drawQueue.AddShaderParameter(VSP_SHBR, sh.Br_);
        drawQueue.AddShaderParameter(VSP_SHBG, sh.Bg_);
        drawQueue.AddShaderParameter(VSP_SHBB, sh.Bb_);
        drawQueue.AddShaderParameter(VSP_SHC, sh.C_);
        drawQueue.AddShaderParameter(VSP_MODEL, *sourceBatch.worldTransform_);
        drawQueue.CommitShaderParameterGroup(SP_OBJECT);
        }

        if (drawQueue.BeginShaderParameterGroup(SP_LIGHT, first))
        {
            first = false;
        Node* lightNode = light->GetNode();
        float atten = 1.0f / Max(light->GetRange(), M_EPSILON);
        Vector3 lightDir(lightNode->GetWorldRotation() * Vector3::BACK);
        Vector4 lightPos(lightNode->GetWorldPosition(), atten);

        drawQueue.AddShaderParameter(VSP_LIGHTDIR, lightDir);
        drawQueue.AddShaderParameter(VSP_LIGHTPOS, lightPos);

        float fade = 1.0f;
        float fadeEnd = light->GetDrawDistance();
        float fadeStart = light->GetFadeDistance();

        // Do fade calculation for light if both fade & draw distance defined
        if (light->GetLightType() != LIGHT_DIRECTIONAL && fadeEnd > 0.0f && fadeStart > 0.0f && fadeStart < fadeEnd)
            fade = Min(1.0f - (light->GetDistance() - fadeStart) / (fadeEnd - fadeStart), 1.0f);

        // Negative lights will use subtract blending, so write absolute RGB values to the shader parameter
        drawQueue.AddShaderParameter(PSP_LIGHTCOLOR, Color(light->GetEffectiveColor().Abs(),
            light->GetEffectiveSpecularIntensity()) * fade);
        drawQueue.AddShaderParameter(PSP_LIGHTDIR, lightDir);
        drawQueue.AddShaderParameter(PSP_LIGHTPOS, lightPos);
        drawQueue.AddShaderParameter(PSP_LIGHTRAD, light->GetRadius());
        drawQueue.AddShaderParameter(PSP_LIGHTLENGTH, light->GetLength());

        Vector4 vertexLights[MAX_VERTEX_LIGHTS * 3]{};
        for (unsigned i = 0; i < batch.vertexLights_.size(); ++i)
        {
            Light* vertexLight = batch.vertexLights_[i];
            if (!vertexLight)
                continue;
            Node* vertexLightNode = vertexLight->GetNode();
            LightType type = vertexLight->GetLightType();

            // Attenuation
            float invRange, cutoff, invCutoff;
            if (type == LIGHT_DIRECTIONAL)
                invRange = 0.0f;
            else
                invRange = 1.0f / Max(vertexLight->GetRange(), M_EPSILON);
            if (type == LIGHT_SPOT)
            {
                cutoff = Cos(vertexLight->GetFov() * 0.5f);
                invCutoff = 1.0f / (1.0f - cutoff);
            }
            else
            {
                cutoff = -2.0f;
                invCutoff = 1.0f;
            }

            // Color
            float fade = 1.0f;
            float fadeEnd = vertexLight->GetDrawDistance();
            float fadeStart = vertexLight->GetFadeDistance();

            // Do fade calculation for light if both fade & draw distance defined
            if (vertexLight->GetLightType() != LIGHT_DIRECTIONAL && fadeEnd > 0.0f && fadeStart > 0.0f && fadeStart < fadeEnd)
                fade = Min(1.0f - (vertexLight->GetDistance() - fadeStart) / (fadeEnd - fadeStart), 1.0f);

            Color color = vertexLight->GetEffectiveColor() * fade;
            vertexLights[i * 3] = Vector4(color.r_, color.g_, color.b_, invRange);

            // Direction
            vertexLights[i * 3 + 1] = Vector4(-(vertexLightNode->GetWorldDirection()), cutoff);

            // Position
            vertexLights[i * 3 + 2] = Vector4(vertexLightNode->GetWorldPosition(), invCutoff);
        }

        drawQueue.AddShaderParameter(VSP_VERTEXLIGHTS, ea::span<const Vector4>{ vertexLights });
        drawQueue.CommitShaderParameterGroup(SP_LIGHT);
        }

        drawQueue.SetBuffers(sourceBatch.geometry_->GetVertexBuffers(), sourceBatch.geometry_->GetIndexBuffer());

        drawQueue.DrawIndexed(sourceBatch.geometry_->GetIndexStart(), sourceBatch.geometry_->GetIndexCount());
    }

    drawQueue.Execute(graphics_);

    if (renderTarget_)
    {
        // On OpenGL, flip the projection if rendering to a texture so that the texture can be addressed in the same way
        // as a render texture produced on Direct3D9
#ifdef URHO3D_OPENGL
        if (camera_)
            camera_->SetFlipVertical(false);
#endif
    }
}

}
