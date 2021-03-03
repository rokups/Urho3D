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
#include "../Graphics/DrawCommandQueue.h"
#include "../Graphics/OcclusionBuffer.h"
#include "../Graphics/Octree.h"
#include "../Graphics/OctreeQuery.h"
#include "../Graphics/RenderSurface.h"
#include "../Graphics/Technique.h"
#include "../Graphics/Viewport.h"
#include "../RenderPipeline/DrawableProcessor.h"
#include "../RenderPipeline/RenderPipelineInterface.h"
#include "../RenderPipeline/SceneProcessor.h"
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

SceneProcessor::SceneProcessor(RenderPipelineInterface* renderPipeline, const ea::string& shadowTechnique)
    : Object(renderPipeline->GetContext())
    , drawableProcessor_(MakeShared<DrawableProcessor>(renderPipeline))
    , batchCompositor_(MakeShared<BatchCompositor>(renderPipeline, drawableProcessor_, Technique::GetPassIndex("shadow")))
    , shadowMapAllocator_(MakeShared<ShadowMapAllocator>(context_))
    , instancingBuffer_(MakeShared<InstancingBuffer>(context_))
    , batchRenderer_(MakeShared<BatchRenderer>(context_, drawableProcessor_, instancingBuffer_))
    , drawQueue_(renderPipeline->GetDefaultDrawQueue())
{
    renderPipeline->OnUpdateBegin.Subscribe(this, &SceneProcessor::OnUpdateBegin);
}

SceneProcessor::~SceneProcessor()
{

}

void SceneProcessor::Define(RenderSurface* renderTarget, Viewport* viewport)
{
    Camera* camera = viewport->GetCamera();
    auto workQueue = context_->GetSubsystem<WorkQueue>();

    frameInfo_.numThreads_ = workQueue->GetNumThreads() + 1;
    frameInfo_.frameNumber_ = 0;
    frameInfo_.timeStep_ = 0.0f;

    frameInfo_.viewport_ = viewport;
    frameInfo_.renderTarget_ = renderTarget;

    frameInfo_.scene_ = viewport->GetScene();
    frameInfo_.camera_ = viewport->GetCamera();
    frameInfo_.cullCamera_ = viewport->GetCullCamera() ? viewport->GetCullCamera() : frameInfo_.camera_;
    frameInfo_.octree_ = frameInfo_.scene_ ? frameInfo_.scene_->GetComponent<Octree>() : nullptr;

    frameInfo_.viewRect_ = viewport->GetEffectiveRect(renderTarget);
    frameInfo_.viewSize_ = frameInfo_.viewRect_.Size();
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
        shadowMapAllocator_->SetSettings(settings_);
        instancingBuffer_->SetSettings(settings_);
        batchRenderer_->SetSettings(settings_);
        batchCompositor_->SetShadowMaterialQuality(settings_.materialQuality_);
    }
}

void SceneProcessor::UpdateFrameInfo(const FrameInfo& frameInfo)
{
    frameInfo_.frameNumber_ = frameInfo.frameNumber_;
    frameInfo_.timeStep_ = frameInfo.timeStep_;
}

void SceneProcessor::Update()
{
    // Collect occluders
    // TODO(renderer): Make optional
    activeOcclusionBuffer_ = nullptr;
    if (settings_.maxOccluderTriangles_ > 0)
    {
        OccluderOctreeQuery occluderQuery(occluders_,
            frameInfo_.cullCamera_->GetFrustum(), frameInfo_.cullCamera_->GetViewMask());
        frameInfo_.octree_->GetDrawables(occluderQuery);
        drawableProcessor_->ProcessOccluders(occluders_, settings_.occluderSizeThreshold_);

        if (drawableProcessor_->HasOccluders())
        {
            if (!occlusionBuffer_)
                occlusionBuffer_ = MakeShared<OcclusionBuffer>(context_);
            const IntVector2 bufferSize = CalculateOcclusionBufferSize(settings_.occlusionBufferSize_, frameInfo_.cullCamera_);
            occlusionBuffer_->SetSize(bufferSize.x_, bufferSize.y_, settings_.threadedOcclusion_);
            occlusionBuffer_->SetView(frameInfo_.cullCamera_);

            DrawOccluders();
            if (occlusionBuffer_->GetNumTriangles() > 0)
                activeOcclusionBuffer_ = occlusionBuffer_;
        }
    }

    // Collect visible drawables
    if (activeOcclusionBuffer_)
    {
        OccludedFrustumOctreeQuery query(drawables_, frameInfo_.cullCamera_->GetFrustum(),
            activeOcclusionBuffer_, DRAWABLE_GEOMETRY | DRAWABLE_LIGHT, frameInfo_.cullCamera_->GetViewMask());
        frameInfo_.octree_->GetDrawables(query);
    }
    else
    {
        FrustumOctreeQuery drawableQuery(drawables_, frameInfo_.cullCamera_->GetFrustum(),
            DRAWABLE_GEOMETRY | DRAWABLE_LIGHT, frameInfo_.cullCamera_->GetViewMask());
        frameInfo_.octree_->GetDrawables(drawableQuery);
    }

    // Process drawables
    drawableProcessor_->ProcessVisibleDrawables(drawables_, activeOcclusionBuffer_);
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

    BatchRenderFlags flags = BatchRenderFlag::None;
    if (settings_.enableInstancing_)
        flags |= BatchRenderFlag::EnableInstancingForStaticGeometry;

    const auto& visibleLights = drawableProcessor_->GetLightProcessors();
    for (LightProcessor* sceneLight : visibleLights)
    {
        for (const ShadowSplitProcessor& split : sceneLight->GetSplits())
        {
            split.SortShadowBatches(sortedShadowBatches_);

            drawQueue_->Reset();

            instancingBuffer_->Begin();
            batchRenderer_->RenderBatches({ *drawQueue_, split }, flags, sortedShadowBatches_);
            instancingBuffer_->End();

            shadowMapAllocator_->BeginShadowMapRendering(split.GetShadowMap());
            drawQueue_->Execute();
        }
    }
}

void SceneProcessor::OnUpdateBegin(const FrameInfo& frameInfo)
{
    shadowMapAllocator_->ResetAllShadowMaps();
    occluders_.clear();
    drawables_.clear();
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
