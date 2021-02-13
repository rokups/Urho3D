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

}

SceneProcessor::SceneProcessor(RenderPipelineInterface* renderPipeline, const ea::string& shadowTechnique)
    : Object(renderPipeline->GetContext())
    , renderPipeline_(renderPipeline)
    , drawableProcessor_(MakeShared<DrawableProcessor>(renderPipeline_))
    , batchCompositor_(MakeShared<BatchCompositor>(renderPipeline_, drawableProcessor_, Technique::GetPassIndex("shadow")))
{
    renderPipeline_->OnUpdateBegin.Subscribe(this, &SceneProcessor::OnUpdateBegin);
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

void SceneProcessor::UpdateFrameInfo(const FrameInfo& frameInfo)
{
    frameInfo_.frameNumber_ = frameInfo.frameNumber_;
    frameInfo_.timeStep_ = frameInfo.timeStep_;
}

void SceneProcessor::Update()
{
    // Collect occluders
    // TODO(renderer): Make optional
    OccluderOctreeQuery occluderQuery(occluders_,
        frameInfo_.cullCamera_->GetFrustum(), frameInfo_.cullCamera_->GetViewMask());
    frameInfo_.octree_->GetDrawables(occluderQuery);

    // Collect visible drawables
    // TODO(renderer): Add occlusion culling
    FrustumOctreeQuery drawableQuery(drawables_, frameInfo_.cullCamera_->GetFrustum(),
        DRAWABLE_GEOMETRY | DRAWABLE_LIGHT, frameInfo_.cullCamera_->GetViewMask());
    frameInfo_.octree_->GetDrawables(drawableQuery);

    // Process drawables
    drawableProcessor_->ProcessVisibleDrawables(drawables_);
    drawableProcessor_->ProcessLights(renderPipeline_);

    const auto& lightProcessors = drawableProcessor_->GetLightProcessors();
    for (unsigned i = 0; i < lightProcessors.size(); ++i)
        drawableProcessor_->ProcessForwardLighting(i, lightProcessors[i]->GetLitGeometries());

    drawableProcessor_->UpdateGeometries();

    batchCompositor_->ComposeBatches();
    batchCompositor_->ComposeShadowBatches(lightProcessors);
}

void SceneProcessor::OnUpdateBegin(const FrameInfo& frameInfo)
{
    occluders_.clear();
    drawables_.clear();
}

}
