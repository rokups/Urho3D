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
#include "../Graphics/Drawable.h"
#include "../Graphics/DrawCommandQueue.h"
#include "../Graphics/OcclusionBuffer.h"
#include "../Graphics/Octree.h"
#include "../Graphics/OctreeQuery.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/RenderSurface.h"
#include "../Graphics/Technique.h"
#include "../Graphics/Viewport.h"
#include "../RenderPipeline/BatchCompositor.h"
#include "../RenderPipeline/BatchRenderer.h"
#include "../RenderPipeline/CameraProcessor.h"
#include "../RenderPipeline/DrawableProcessor.h"
#include "../RenderPipeline/InstancingBuffer.h"
#include "../RenderPipeline/LightProcessor.h"
#include "../RenderPipeline/PipelineBatchSortKey.h"
#include "../RenderPipeline/PipelineStateBuilder.h"
#include "../RenderPipeline/RenderPipelineDebugger.h"
#include "../RenderPipeline/RenderPipelineDefs.h"
#include "../RenderPipeline/ScenePass.h"
#include "../RenderPipeline/SceneProcessor.h"
#include "../RenderPipeline/ShadowMapAllocator.h"
#include "../Scene/Scene.h"

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

class OccluderOctreeQuery : public FrustumOctreeQuery
{
public:
    /// Construct with frustum and query parameters.
    OccluderOctreeQuery(ea::vector<Drawable*>& result, const Frustum& frustum, unsigned viewMask = DEFAULT_VIEWMASK) :
        FrustumOctreeQuery(result, frustum, DRAWABLE_GEOMETRY, viewMask)
    {
    }

    /// Intersection test for drawables.
    void TestDrawables(Drawable** start, Drawable** end, bool inside) override
    {
        for (Drawable* drawable : MakeIteratorRange(start, end))
        {
            const DrawableFlags flags = drawable->GetDrawableFlags();
            if (flags == DRAWABLE_GEOMETRY && drawable->IsOccluder() && (drawable->GetViewMask() & viewMask_))
            {
                if (inside || frustum_.IsInsideFast(drawable->GetWorldBoundingBox()))
                    result_.push_back(drawable);
            }
        }
    }
};

class OccludedFrustumOctreeQuery : public FrustumOctreeQuery
{
public:
    /// Construct with frustum, occlusion buffer and query parameters.
    OccludedFrustumOctreeQuery(ea::vector<Drawable*>& result, const Frustum& frustum, OcclusionBuffer* buffer,
                               DrawableFlags drawableFlags = DRAWABLE_ANY, unsigned viewMask = DEFAULT_VIEWMASK) :
        FrustumOctreeQuery(result, frustum, drawableFlags, viewMask),
        buffer_(buffer)
    {
    }

    /// Intersection test for an octant.
    Intersection TestOctant(const BoundingBox& box, bool inside) override
    {
        if (inside)
            return buffer_->IsVisible(box) ? INSIDE : OUTSIDE;
        else
        {
            Intersection result = frustum_.IsInside(box);
            if (result != OUTSIDE && !buffer_->IsVisible(box))
                result = OUTSIDE;
            return result;
        }
    }

    /// Intersection test for drawables. Note: drawable occlusion is performed later in worker threads.
    void TestDrawables(Drawable** start, Drawable** end, bool inside) override
    {
        for (Drawable* drawable : MakeIteratorRange(start, end))
        {
            if ((drawable->GetDrawableFlags() & drawableFlags_) && (drawable->GetViewMask() & viewMask_))
            {
                if (inside || frustum_.IsInsideFast(drawable->GetWorldBoundingBox()))
                    result_.push_back(drawable);
            }
        }
    }

    /// Occlusion buffer.
    OcclusionBuffer* buffer_;
};

IntVector2 CalculateOcclusionBufferSize(unsigned size, Camera* cullCamera)
{
    const auto width = static_cast<int>(size);
    const int height = RoundToInt(size / cullCamera->GetAspectRatio());
    return { width, height };
}

BoundingBox GetViewSpaceLightBoundingBox(Light* light, Camera* camera)
{
    const Matrix3x4& view = camera->GetView();

    switch (light->GetLightType())
    {
    case LIGHT_POINT:
    {
        const Vector3 center = view * light->GetNode()->GetWorldPosition();
        const float extent = 0.58f * light->GetRange();
        return BoundingBox{ center - extent * Vector3::ONE, center + extent * Vector3::ONE };
    }
    case LIGHT_SPOT:
    {
        const Frustum lightFrustum = light->GetViewSpaceFrustum(view);
        return BoundingBox{ &lightFrustum.vertices_[4], 4 };
    }
    default:
        assert(0);
        return {};
    }
}

float GetLightSizeInPixels(Light* light, const FrameInfo& frameInfo)
{
    const auto viewSize = static_cast<Vector2>(frameInfo.viewSize_);
    const LightType lightType = light->GetLightType();
    if (lightType == LIGHT_DIRECTIONAL)
        return ea::max(viewSize.x_, viewSize.y_);

    const BoundingBox lightBox = GetViewSpaceLightBoundingBox(light, frameInfo.camera_);
    const Matrix4& projection = frameInfo.camera_->GetProjection();
    const Vector2 projectionSize = 0.5f * lightBox.Projected(projection).Size() * viewSize;
    return ea::max(projectionSize.x_, projectionSize.y_);
}

unsigned GetMaxShadowSplitSize(const SceneProcessorSettings& settings, unsigned pageSize, Light* light)
{
    switch (light->GetLightType())
    {
    case LIGHT_DIRECTIONAL:
        return ea::min(settings.directionalShadowSize_, light->GetNumShadowSplits() == 1 ? pageSize : pageSize / 2);
    case LIGHT_POINT:
        return ea::min(settings.pointShadowSize_, pageSize / 4);
    case LIGHT_SPOT:
    default:
        return ea::min(settings.spotShadowSize_, pageSize);
    }
}

}

SceneProcessor::SceneProcessor(RenderPipelineInterface* renderPipeline, const ea::string& shadowTechnique,
    ShadowMapAllocator* shadowMapAllocator, InstancingBuffer* instancingBuffer)
    : Object(renderPipeline->GetContext())
    , graphics_(GetSubsystem<Graphics>())
    , renderPipeline_(renderPipeline)
    , debugger_(renderPipeline_->GetDebugger())
    , shadowMapAllocator_(shadowMapAllocator)
    , instancingBuffer_(instancingBuffer)
    , drawQueue_(GetSubsystem<Renderer>()->GetDefaultDrawQueue())
    , cameraProcessor_(MakeShared<CameraProcessor>(context_))
    , pipelineStateBuilder_(MakeShared<PipelineStateBuilder>(context_,
        this, cameraProcessor_, shadowMapAllocator_, instancingBuffer_))
    , drawableProcessor_(MakeShared<DrawableProcessor>(renderPipeline_))
    , batchCompositor_(MakeShared<BatchCompositor>(
        renderPipeline_, drawableProcessor_, pipelineStateBuilder_, Technique::GetPassIndex("shadow")))
    , batchRenderer_(MakeShared<BatchRenderer>(renderPipeline_, drawableProcessor_, instancingBuffer_))
    , batchStateCacheCallback_(pipelineStateBuilder_)
{
    renderPipeline_->OnUpdateBegin.Subscribe(this, &SceneProcessor::OnUpdateBegin);
    renderPipeline_->OnRenderBegin.Subscribe(this, &SceneProcessor::OnRenderBegin);
    renderPipeline_->OnRenderEnd.Subscribe(this, &SceneProcessor::OnRenderEnd);
}

SceneProcessor::~SceneProcessor()
{

}

bool SceneProcessor::Define(const CommonFrameInfo& frameInfo)
{
    frameInfo_.frameNumber_ = 0;
    frameInfo_.timeStep_ = 0.0f;

    frameInfo_.viewport_ = frameInfo.viewport_;
    frameInfo_.renderTarget_ = frameInfo.renderTarget_;
    frameInfo_.viewRect_ = frameInfo.viewportRect_;
    frameInfo_.viewSize_ = frameInfo.viewportSize_;

    frameInfo_.scene_ = frameInfo_.viewport_->GetScene();
    frameInfo_.camera_ = frameInfo_.viewport_->GetCullCamera()
        ? frameInfo_.viewport_->GetCullCamera()
        : frameInfo_.viewport_->GetCamera();
    frameInfo_.octree_ = frameInfo_.scene_ ? frameInfo_.scene_->GetComponent<Octree>() : nullptr;

    return frameInfo_.octree_ && frameInfo_.camera_;
}

void SceneProcessor::SetRenderCameras(ea::span<Camera* const> cameras)
{
    cameraProcessor_->SetCameras(cameras);
}

void SceneProcessor::SetRenderCamera(Camera* camera)
{
    Camera* cameras[] = { camera };
    cameraProcessor_->SetCameras(cameras);
}

void SceneProcessor::SetPasses(ea::vector<SharedPtr<ScenePass>> passes)
{
    passes.erase(ea::remove(passes.begin(), passes.end(), nullptr), passes.end());
    passes_ = ea::move(passes);

    ea::vector<SharedPtr<DrawableProcessorPass>> drawableProcessorPasses(passes_.begin(), passes_.end());
    drawableProcessor_->SetPasses(ea::move(drawableProcessorPasses));
    ea::vector<SharedPtr<BatchCompositorPass>> batchCompositorPasses(passes_.begin(), passes_.end());
    batchCompositor_->SetPasses(ea::move(batchCompositorPasses));
}

void SceneProcessor::SetSettings(const ShaderProgramCompositorSettings& settings)
{
    pipelineStateBuilder_->SetSettings(settings);

    if (settings_ != settings.sceneProcessor_)
    {
        settings_ = settings.sceneProcessor_;
        drawableProcessor_->SetSettings(settings.sceneProcessor_);
        batchRenderer_->SetSettings(settings.sceneProcessor_);
        batchCompositor_->SetShadowMaterialQuality(settings.sceneProcessor_.materialQuality_);
    }
}

void SceneProcessor::Update()
{
    // Collect occluders
    currentOcclusionBuffer_ = nullptr;
    if (settings_.maxOccluderTriangles_ > 0)
    {
        URHO3D_PROFILE("ProcessOccluders");

        OccluderOctreeQuery occluderQuery(occluders_,
            frameInfo_.camera_->GetFrustum(), frameInfo_.camera_->GetViewMask());
        frameInfo_.octree_->GetDrawables(occluderQuery);
        drawableProcessor_->ProcessOccluders(occluders_, settings_.occluderSizeThreshold_);

        if (drawableProcessor_->HasOccluders())
        {
            if (!occlusionBuffer_)
                occlusionBuffer_ = MakeShared<OcclusionBuffer>(context_);
            const IntVector2 bufferSize = CalculateOcclusionBufferSize(settings_.occlusionBufferSize_, frameInfo_.camera_);
            occlusionBuffer_->SetSize(bufferSize.x_, bufferSize.y_, settings_.threadedOcclusion_);
            occlusionBuffer_->SetView(frameInfo_.camera_);

            DrawOccluders();
            if (occlusionBuffer_->GetNumTriangles() > 0)
                currentOcclusionBuffer_ = occlusionBuffer_;
        }
    }

    // Collect visible drawables
    if (currentOcclusionBuffer_)
    {
        URHO3D_PROFILE("QueryVisibleDrawables");
        OccludedFrustumOctreeQuery query(drawables_, frameInfo_.camera_->GetFrustum(),
            currentOcclusionBuffer_, DRAWABLE_GEOMETRY | DRAWABLE_LIGHT, frameInfo_.camera_->GetViewMask());
        frameInfo_.octree_->GetDrawables(query);
    }
    else
    {
        URHO3D_PROFILE("QueryVisibleDrawables");
        FrustumOctreeQuery drawableQuery(drawables_, frameInfo_.camera_->GetFrustum(),
            DRAWABLE_GEOMETRY | DRAWABLE_LIGHT, frameInfo_.camera_->GetViewMask());
        frameInfo_.octree_->GetDrawables(drawableQuery);
    }

    // Process drawables
    drawableProcessor_->ProcessVisibleDrawables(drawables_, currentOcclusionBuffer_);
    drawableProcessor_->ProcessLights(this);
    drawableProcessor_->ProcessForwardLighting();

    drawableProcessor_->UpdateGeometries();

    batchCompositor_->ComposeSceneBatches();
    if (settings_.enableShadows_)
        batchCompositor_->ComposeShadowBatches();
    if (settings_.IsDeferredLighting())
        batchCompositor_->ComposeLightVolumeBatches();
}

void SceneProcessor::PrepareInstancingBuffer()
{
    if (!instancingBuffer_->IsEnabled())
        return;

    URHO3D_PROFILE("PrepareInstancingBuffer");

    instancingBuffer_->Begin();

    const auto& visibleLights = drawableProcessor_->GetLightProcessors();

    for (LightProcessor* sceneLight : visibleLights)
    {
        for (ShadowSplitProcessor& split : sceneLight->GetMutableSplits())
            batchRenderer_->PrepareInstancingBuffer(split.GetMutableShadowBatches());
    }

    for (ScenePass* pass : passes_)
        pass->PrepareInstacingBuffer(batchRenderer_);

    instancingBuffer_->End();
}

void SceneProcessor::RenderShadowMaps()
{
    if (!settings_.enableShadows_)
        return;

    URHO3D_PROFILE("RenderShadowMaps");

    const auto& lightsByShadowMap = drawableProcessor_->GetLightProcessorsByShadowMap();
    for (LightProcessor* sceneLight : lightsByShadowMap)
    {
        for (const ShadowSplitProcessor& split : sceneLight->GetSplits())
        {
            if (RenderPipelineDebugger::IsSnapshotInProgress(debugger_))
            {
                const ea::string passName = Format("ShadowMap.[{}].{}",
                    split.GetLight()->GetFullNameDebug(), split.GetSplitIndex());
                debugger_->BeginPass(passName);
            }

            drawQueue_->Reset();
            batchRenderer_->RenderBatches({ *drawQueue_, split }, split.GetShadowBatches());
            shadowMapAllocator_->BeginShadowMapRendering(split.GetShadowMap());
            drawQueue_->Execute();

            if (RenderPipelineDebugger::IsSnapshotInProgress(debugger_))
            {
                debugger_->EndPass();
            }
        }
    }
}

void SceneProcessor::RenderSceneBatches(ea::string_view debugName, Camera* camera,
    const PipelineBatchGroup<PipelineBatchByState>& batchGroup,
    ea::span<const ShaderResourceDesc> globalResources, ea::span<const ShaderParameterDesc> cameraParameters)
{
    RenderBatchesInternal(debugName, camera, batchGroup, globalResources, cameraParameters);
}

void SceneProcessor::RenderSceneBatches(ea::string_view debugName, Camera* camera,
    const PipelineBatchGroup<PipelineBatchBackToFront>& batchGroup,
    ea::span<const ShaderResourceDesc> globalResources, ea::span<const ShaderParameterDesc> cameraParameters)
{
    RenderBatchesInternal(debugName, camera, batchGroup, globalResources, cameraParameters);
}

void SceneProcessor::RenderLightVolumeBatches(ea::string_view debugName, Camera* camera,
    ea::span<const ShaderResourceDesc> globalResources, ea::span<const ShaderParameterDesc> cameraParameters)
{
    if (RenderPipelineDebugger::IsSnapshotInProgress(debugger_))
        debugger_->BeginPass(debugName);

    drawQueue_->Reset();

    BatchRenderingContext ctx{ *drawQueue_, *camera };
    ctx.globalResources_ = globalResources;
    ctx.cameraParameters_ = cameraParameters;

    batchRenderer_->RenderLightVolumeBatches(ctx, GetLightVolumeBatches());

    graphics_->SetClipPlane(false);
    drawQueue_->Execute();

    if (RenderPipelineDebugger::IsSnapshotInProgress(debugger_))
        debugger_->EndPass();
}

template <class T>
void SceneProcessor::RenderBatchesInternal(ea::string_view debugName, Camera* camera, const PipelineBatchGroup<T>& batchGroup,
    ea::span<const ShaderResourceDesc> globalResources, ea::span<const ShaderParameterDesc> cameraParameters)
{
    if (RenderPipelineDebugger::IsSnapshotInProgress(debugger_))
        debugger_->BeginPass(debugName);

    drawQueue_->Reset();

    BatchRenderingContext ctx{ *drawQueue_, *camera };
    ctx.globalResources_ = globalResources;
    ctx.cameraParameters_ = cameraParameters;

    batchRenderer_->RenderBatches(ctx, batchGroup);

    graphics_->SetClipPlane(camera->GetUseClipping(),
        camera->GetClipPlane(), camera->GetView(), camera->GetGPUProjection());
    drawQueue_->Execute();

    if (RenderPipelineDebugger::IsSnapshotInProgress(debugger_))
        debugger_->EndPass();
}

BatchCompositorPass* SceneProcessor::GetUserPass(Object* pass) const
{
    assert(pass);
    if (pass == batchCompositor_.Get())
        return nullptr;
    return static_cast<BatchCompositorPass*>(pass);
}

void SceneProcessor::OnUpdateBegin(const CommonFrameInfo& frameInfo)
{
    frameInfo_.frameNumber_ = frameInfo.frameNumber_;
    frameInfo_.timeStep_ = frameInfo.timeStep_;

    occluders_.clear();
    drawables_.clear();

    cameraProcessor_->OnUpdateBegin(frameInfo_);
    drawableProcessor_->OnUpdateBegin(frameInfo_);
    pipelineStateBuilder_->UpdateFrameSettings();
}

void SceneProcessor::OnRenderBegin(const CommonFrameInfo& frameInfo)
{
    cameraProcessor_->OnRenderBegin(frameInfo_);
}

void SceneProcessor::OnRenderEnd(const CommonFrameInfo& frameInfo)
{
    cameraProcessor_->OnRenderEnd(frameInfo_);
}

bool SceneProcessor::IsLightShadowed(Light* light)
{
    const bool shadowsEnabled = settings_.enableShadows_
        && light->GetCastShadows()
        && light->GetLightImportance() != LI_NOT_IMPORTANT
        && light->GetShadowIntensity() < 1.0f;

    if (!shadowsEnabled)
        return false;

    if (light->GetShadowDistance() > 0.0f && light->GetDistance() > light->GetShadowDistance())
        return false;

    return true;
}

unsigned SceneProcessor::GetShadowMapSize(Light* light, unsigned /*numActiveSplits*/) const
{
    const FocusParameters& parameters = light->GetShadowFocus();
    const float shadowResolutionScale = light->GetShadowResolution();
    const LightType lightType = light->GetLightType();

    const float lightSizeInPixels = GetLightSizeInPixels(light, frameInfo_);
    const unsigned pageSize = shadowMapAllocator_->GetSettings().shadowAtlasPageSize_;
    const unsigned maxShadowSize = GetMaxShadowSplitSize(settings_, pageSize, light);

    const float baseShadowSize = parameters.autoSize_ ? lightSizeInPixels : maxShadowSize;
    const auto shadowSize = NextPowerOfTwo(static_cast<unsigned>(baseShadowSize * shadowResolutionScale));

    return Clamp<unsigned>(shadowSize, SHADOW_MIN_PIXELS, maxShadowSize);
}

ShadowMapRegion SceneProcessor::AllocateTransientShadowMap(const IntVector2& size)
{
    return shadowMapAllocator_->AllocateShadowMap(size);
}

void SceneProcessor::DrawOccluders()
{
    const auto& activeOccluders = drawableProcessor_->GetOccluders();

    occlusionBuffer_->SetMaxTriangles(settings_.maxOccluderTriangles_);
    occlusionBuffer_->Clear();

    if (!occlusionBuffer_->IsThreaded())
    {
        // If not threaded, draw occluders one by one and test the next occluder against already rasterized depth
        for (unsigned i = 0; i < activeOccluders.size(); ++i)
        {
            Drawable* occluder = activeOccluders[i].drawable_;
            if (i > 0)
            {
                // For subsequent occluders, do a test against the pixel-level occlusion buffer to see if rendering is necessary
                if (!occlusionBuffer_->IsVisible(occluder->GetWorldBoundingBox()))
                    continue;
            }

            // Check for running out of triangles
            bool success = occluder->DrawOcclusion(occlusionBuffer_);
            // Draw triangles submitted by this occluder
            occlusionBuffer_->DrawTriangles();
            if (!success)
                break;
        }
    }
    else
    {
        // In threaded mode submit all triangles first, then render (cannot test in this case)
        for (unsigned i = 0; i < activeOccluders.size(); ++i)
        {
            // Check for running out of triangles
            if (!activeOccluders[i].drawable_->DrawOcclusion(occlusionBuffer_))
                break;
        }

        occlusionBuffer_->DrawTriangles();
    }

    // Finally build the depth mip levels
    occlusionBuffer_->BuildDepthHierarchy();
}

}
