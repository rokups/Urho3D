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

#include "../Precompiled.h"

#include "../Core/Context.h"
#include "../Core/IteratorRange.h"
#include "../Graphics/Camera.h"
#include "../Graphics/CustomView.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Viewport.h"
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

/// Get light importance.
// TODO: Move it to Light
LightImportance GetLightImportance(Light* light)
{
    if (light->GetPerVertex())
        return LI_NOT_IMPORTANT;
    else
        return LI_AUTO;
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
            ++numImportantLights_;

        // Add new light
        lights_.emplace(penalty, light);

        // If too many lights, drop the least important one
        const unsigned maxLights = MaxVertexLights + ea::max(ctx.maxPixelLights_, numImportantLights_);
        if (lights_.size() > maxLights)
        {
            // TODO: Update SH
            lights_.pop_back();
        }
    }

    /// Container of per-pixel and per-pixel lights.
    Container lights_;
    /// Accumulated SH lights.
    SphericalHarmonicsDot9 sh_;
    /// Number of important lights.
    unsigned numImportantLights_{};
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
        const TransientDrawableDataIndex& transientData, Light* light)
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
            if (traits & TransientDrawableDataIndex::DrawableVisibleGeometry)
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
    const TransientDrawableDataIndex* transientData_{};
    /// Light mask to check.
    unsigned lightMask_{};
};

/// Frustum Query for spot light.
struct SpotLightLitGeometriesQuery : public FrustumOctreeQuery
{
    /// Construct.
    SpotLightLitGeometriesQuery(ea::vector<Drawable*>& result,
        const TransientDrawableDataIndex& transientData, Light* light)
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
            if (traits & TransientDrawableDataIndex::DrawableVisibleGeometry)
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
    const TransientDrawableDataIndex* transientData_{};
    /// Light mask to check.
    unsigned lightMask_{};
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

    // TODO: Sort lights?
    //const auto compareLights = [](Light* lhs, Light* rhs) { return lhs->GetSortValue() < rhs->GetSortValue(); };
    //ea::sort(cache.visibleLights_.begin(), cache.visibleLights_.end(), compareLights);
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
    graphics_->Clear(CLEAR_COLOR | CLEAR_DEPTH | CLEAR_DEPTH, Color::RED * 0.5f);

    script_->Render(this);

    // Collect and process visible drawables
    DrawableCollection drawablesInMainCamera;
    CollectDrawables(drawablesInMainCamera, camera_, DRAWABLE_GEOMETRY | DRAWABLE_LIGHT);

    DrawableViewportCache viewportCache;
    ProcessPrimaryDrawables(viewportCache, drawablesInMainCamera, camera_);

    // Process visible lights
    ea::vector<DrawableLightCache> globalLightCache;
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
    ea::vector<DrawableLightAccumulator<4>> lightAccumulator;
    lightAccumulator.resize(numDrawables_);

    viewportCache.visibleLights_.ForEach([&](unsigned lightIndex, Light* light)
    {
        DrawableLightAccumulatorContext ctx;
        ctx.maxPixelLights_ = 1;

        DrawableLightCache& lightCache = globalLightCache[lightIndex];
        for (Drawable* litGeometry : lightCache.litGeometries_)
        {
            const unsigned drawableIndex = litGeometry->GetDrawableIndex();
            lightAccumulator[drawableIndex].AccumulateLight(ctx, 1.0f, light);
        }
    });

    auto renderer = context_->GetRenderer();
    //auto graphics = context_->GetGraphics();
    BatchQueue queue;
    viewportCache.visibleGeometries_.ForEach([&](unsigned index, Drawable* drawable)
    {
            for (const SourceBatch& sourceBatch : drawable->GetBatches())
            {
                Batch destBatch(sourceBatch);
                auto tech = destBatch.material_->GetTechnique(0);
                destBatch.zone_ = renderer->GetDefaultZone();
                destBatch.pass_ = tech->GetSupportedPass(0);
                destBatch.isBase_ = true;
                destBatch.lightMask_ = 0xffffffff;
                renderer->SetBatchShaders(destBatch, tech, false, queue);
                destBatch.CalculateSortKey();
                //queue.batches_.push_back(destBatch);

                //view->SetGlobalShaderParameters();
                graphics_->SetShaderParameter(VSP_DELTATIME, frameInfo_.timeStep_);
                graphics_->SetShaderParameter(PSP_DELTATIME, frameInfo_.timeStep_);

                if (scene_)
                {
                    float elapsedTime = scene_->GetElapsedTime();
                    graphics_->SetShaderParameter(VSP_ELAPSEDTIME, elapsedTime);
                    graphics_->SetShaderParameter(PSP_ELAPSEDTIME, elapsedTime);
                }

                graphics_->SetShaders(destBatch.vertexShader_, destBatch.pixelShader_);
                graphics_->SetBlendMode(BLEND_REPLACE, false);
                renderer->SetCullMode(destBatch.material_->GetCullMode(), camera_);
                graphics_->SetFillMode(FILL_SOLID);
                graphics_->SetDepthTest(CMP_LESSEQUAL);
                graphics_->SetDepthWrite(true);
                if (graphics_->NeedParameterUpdate(SP_CAMERA, camera_))
                {
                    //view->SetCameraShaderParameters(camera_);
                        Matrix3x4 cameraEffectiveTransform = camera_->GetEffectiveWorldTransform();

    graphics_->SetShaderParameter(VSP_CAMERAPOS, cameraEffectiveTransform.Translation());
    graphics_->SetShaderParameter(VSP_VIEWINV, cameraEffectiveTransform);
    graphics_->SetShaderParameter(VSP_VIEW, camera_->GetView());
    graphics_->SetShaderParameter(PSP_CAMERAPOS, cameraEffectiveTransform.Translation());

    float nearClip = camera_->GetNearClip();
    float farClip = camera_->GetFarClip();
    graphics_->SetShaderParameter(VSP_NEARCLIP, nearClip);
    graphics_->SetShaderParameter(VSP_FARCLIP, farClip);
    graphics_->SetShaderParameter(PSP_NEARCLIP, nearClip);
    graphics_->SetShaderParameter(PSP_FARCLIP, farClip);

    Vector4 depthMode = Vector4::ZERO;
    if (camera_->IsOrthographic())
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
        depthMode.w_ = 1.0f / camera_->GetFarClip();

    graphics_->SetShaderParameter(VSP_DEPTHMODE, depthMode);

    Vector4 depthReconstruct
        (farClip / (farClip - nearClip), -nearClip / (farClip - nearClip), camera_->IsOrthographic() ? 1.0f : 0.0f,
            camera_->IsOrthographic() ? 0.0f : 1.0f);
    graphics_->SetShaderParameter(PSP_DEPTHRECONSTRUCT, depthReconstruct);

    Vector3 nearVector, farVector;
    camera_->GetFrustumSize(nearVector, farVector);
    graphics_->SetShaderParameter(VSP_FRUSTUMSIZE, farVector);

    Matrix4 projection = camera_->GetGPUProjection();
#ifdef URHO3D_OPENGL
    // Add constant depth bias manually to the projection matrix due to glPolygonOffset() inconsistency
    float constantBias = 2.0f * graphics_->GetDepthConstantBias();
    projection.m22_ += projection.m32_ * constantBias;
    projection.m23_ += projection.m33_ * constantBias;
#endif

    graphics_->SetShaderParameter(VSP_VIEWPROJ, projection * camera_->GetView());
                    // During renderpath commands the G-Buffer or viewport texture is assumed to always be viewport-sized
                    //view->SetGBufferShaderParameters(viewSize, IntRect(0, 0, viewSize.x_, viewSize.y_));
                }
                graphics_->SetShaderParameter(VSP_SHAR, destBatch.shaderParameters_.ambient_.Ar_);
                graphics_->SetShaderParameter(VSP_SHAG, destBatch.shaderParameters_.ambient_.Ag_);
                graphics_->SetShaderParameter(VSP_SHAB, destBatch.shaderParameters_.ambient_.Ab_);
                graphics_->SetShaderParameter(VSP_SHBR, destBatch.shaderParameters_.ambient_.Br_);
                graphics_->SetShaderParameter(VSP_SHBG, destBatch.shaderParameters_.ambient_.Bg_);
                graphics_->SetShaderParameter(VSP_SHBB, destBatch.shaderParameters_.ambient_.Bb_);
                graphics_->SetShaderParameter(VSP_SHC, destBatch.shaderParameters_.ambient_.C_);
                graphics_->SetShaderParameter(VSP_MODEL, *destBatch.worldTransform_);
                //graphics_->SetShaderParameter(VSP_AMBIENTSTARTCOLOR, destBatch.zone_->GetAmbientStartColor());
                graphics_->SetShaderParameter(VSP_AMBIENTSTARTCOLOR, Color::WHITE);
                graphics_->SetShaderParameter(VSP_AMBIENTENDCOLOR, Vector4::ZERO);
                graphics_->SetShaderParameter(VSP_ZONE, Matrix3x4::IDENTITY);
                //graphics_->SetShaderParameter(PSP_AMBIENTCOLOR, destBatch.zone_->GetAmbientColor());
                graphics_->SetShaderParameter(PSP_AMBIENTCOLOR, Color::WHITE);
                graphics_->SetShaderParameter(PSP_FOGCOLOR, destBatch.zone_->GetFogColor());

                float farClip = camera_->GetFarClip();
                float fogStart = Min(destBatch.zone_->GetFogStart(), farClip);
                float fogEnd = Min(destBatch.zone_->GetFogEnd(), farClip);
                if (fogStart >= fogEnd * (1.0f - M_LARGE_EPSILON))
                    fogStart = fogEnd * (1.0f - M_LARGE_EPSILON);
                float fogRange = Max(fogEnd - fogStart, M_EPSILON);
                Vector4 fogParams(fogEnd / farClip, farClip / fogRange, 0.0f, 0.0f);

                graphics_->SetShaderParameter(PSP_FOGPARAMS, fogParams);

                // Set material-specific shader parameters and textures
                if (destBatch.material_)
                {
                    if (graphics_->NeedParameterUpdate(SP_MATERIAL, reinterpret_cast<const void*>(destBatch.material_->GetShaderParameterHash())))
                    {
                        const ea::unordered_map<StringHash, MaterialShaderParameter>& parameters = destBatch.material_->GetShaderParameters();
                        for (auto i = parameters.begin(); i !=
                            parameters.end(); ++i)
                            graphics_->SetShaderParameter(i->first, i->second.value_);
                    }

                    const ea::unordered_map<TextureUnit, SharedPtr<Texture> >& textures = destBatch.material_->GetTextures();
                    for (auto i = textures.begin(); i !=
                        textures.end(); ++i)
                    {
                        if (i->first == TU_EMISSIVE && destBatch.lightmapScaleOffset_)
                            continue;

                        if (graphics_->HasTextureUnit(i->first))
                            graphics_->SetTexture(i->first, i->second.Get());
                    }

                    /*if (destBatch.lightmapScaleOffset_)
                    {
                        if (Scene* scene = view->GetScene())
                            graphics_->SetTexture(TU_EMISSIVE, scene->GetLightmapTexture(destBatch.lightmapIndex_));
                    }*/
                    destBatch.geometry_->Draw(graphics_);
                }
            }
    });
}

}
