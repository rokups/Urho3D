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
#include "../RenderPipeline/RenderPipelineDefs.h"
#include "../RenderPipeline/SceneProcessor.h"
#include "../RenderPipeline/ShadowMapAllocator.h"
#include "../Scene/Scene.h"

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

/// %Frustum octree query for occluders.
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

/// %Frustum octree query with occlusion.
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

/// Calculate occlusion buffer size.
IntVector2 CalculateOcclusionBufferSize(unsigned size, Camera* cullCamera)
{
    const auto width = static_cast<int>(size);
    const int height = RoundToInt(size / cullCamera->GetAspectRatio());
    return { width, height };
}

}

SceneProcessor::SceneProcessor(RenderPipelineInterface* renderPipeline, const ea::string& shadowTechnique,
    ShadowMapAllocator* shadowMapAllocator, InstancingBuffer* instancingBuffer)
    : Object(renderPipeline->GetContext())
    , renderPipeline_(renderPipeline)
    , shadowMapAllocator_(shadowMapAllocator)
    , instancingBuffer_(instancingBuffer)
    , drawQueue_(GetSubsystem<Renderer>()->GetDefaultDrawQueue())
    , cameraProcessor_(MakeShared<CameraProcessor>(context_))
    , pipelineStateBuilder_(MakeShared<PipelineStateBuilder>(context_,
        this, cameraProcessor_, shadowMapAllocator_, instancingBuffer_))
    , drawableProcessor_(MakeShared<DrawableProcessor>(renderPipeline_))
    , batchCompositor_(MakeShared<BatchCompositor>(
        renderPipeline_, drawableProcessor_, pipelineStateBuilder_, Technique::GetPassIndex("shadow")))
    , batchRenderer_(MakeShared<BatchRenderer>(context_, drawableProcessor_, instancingBuffer_))
    , batchStateCacheCallback_(pipelineStateBuilder_)
{
    renderPipeline_->OnUpdateBegin.Subscribe(this, &SceneProcessor::OnUpdateBegin);
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

void SceneProcessor::SetPasses(ea::vector<SharedPtr<BatchCompositorPass>> passes)
{
    ea::vector<SharedPtr<DrawableProcessorPass>> drawableProcessorPasses(passes.begin(), passes.end());
    drawableProcessor_->SetPasses(ea::move(drawableProcessorPasses));
    batchCompositor_->SetPasses(ea::move(passes));
}

void SceneProcessor::SetSettings(const SceneProcessorSettings& settings)
{
    if (settings_ != settings)
    {
        settings_ = settings;
        drawableProcessor_->SetSettings(settings_);
        batchRenderer_->SetSettings(settings_);
        batchCompositor_->SetShadowMaterialQuality(settings_.materialQuality_);
    }
}

void SceneProcessor::Update()
{
    // Collect occluders
    // TODO(renderer): Make optional
    currentOcclusionBuffer_ = nullptr;
    if (settings_.maxOccluderTriangles_ > 0)
    {
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
        OccludedFrustumOctreeQuery query(drawables_, frameInfo_.camera_->GetFrustum(),
            currentOcclusionBuffer_, DRAWABLE_GEOMETRY | DRAWABLE_LIGHT, frameInfo_.camera_->GetViewMask());
        frameInfo_.octree_->GetDrawables(query);
    }
    else
    {
        FrustumOctreeQuery drawableQuery(drawables_, frameInfo_.camera_->GetFrustum(),
            DRAWABLE_GEOMETRY | DRAWABLE_LIGHT, frameInfo_.camera_->GetViewMask());
        frameInfo_.octree_->GetDrawables(drawableQuery);
    }

    // Process drawables
    drawableProcessor_->ProcessVisibleDrawables(drawables_, currentOcclusionBuffer_);
    drawableProcessor_->ProcessLights(this);

    const auto& lightProcessors = drawableProcessor_->GetLightProcessors();
    for (unsigned i = 0; i < lightProcessors.size(); ++i)
    {
        const LightProcessor* lightProcessor = lightProcessors[i];
        if (lightProcessor->HasForwardLitGeometries())
            drawableProcessor_->ProcessForwardLighting(i, lightProcessor->GetLitGeometries());
    }

    drawableProcessor_->UpdateGeometries();

    batchCompositor_->ComposeSceneBatches();
    if (settings_.enableShadows_)
        batchCompositor_->ComposeShadowBatches();
    if (settings_.deferredLighting_)
        batchCompositor_->ComposeLightVolumeBatches();
}

void SceneProcessor::RenderShadowMaps()
{
    if (!settings_.enableShadows_)
        return;

    const auto& visibleLights = drawableProcessor_->GetLightProcessors();
    for (LightProcessor* sceneLight : visibleLights)
    {
        for (const ShadowSplitProcessor& split : sceneLight->GetSplits())
        {
            split.SortShadowBatches(tempSortedShadowBatches_);

            drawQueue_->Reset();

            instancingBuffer_->Begin();
            batchRenderer_->RenderBatches({ *drawQueue_, split },
                BatchRenderFlag::EnableInstancingForStaticGeometry, tempSortedShadowBatches_);
            instancingBuffer_->End();

            shadowMapAllocator_->BeginShadowMapRendering(split.GetShadowMap());
            drawQueue_->Execute();
        }
    }
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

unsigned SceneProcessor::GetShadowMapSize(Light* light) const
{
    // TODO(renderer): Implement me
    return light->GetLightType() != LIGHT_POINT ? 512 : 256;
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
