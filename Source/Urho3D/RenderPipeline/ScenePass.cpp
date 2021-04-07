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
#include "../Core/StringUtils.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/Technique.h"
#include "../RenderPipeline/ScenePass.h"

#include <EASTL/sort.h>

#include "../DebugNew.h"

namespace Urho3D
{

ScenePass::ScenePass(RenderPipelineInterface* renderPipeline, DrawableProcessor* drawableProcessor,
    BatchStateCacheCallback* callback, DrawableProcessorPassFlags flags, const ea::string& deferredPass,
    const ea::string& unlitBasePass, const ea::string& litBasePass, const ea::string& lightPass)
    : BatchCompositorPass(renderPipeline, drawableProcessor, callback, flags, Technique::GetPassIndex(deferredPass),
        Technique::GetPassIndex(unlitBasePass), Technique::GetPassIndex(litBasePass), Technique::GetPassIndex(lightPass))
{
}

ScenePass::ScenePass(RenderPipelineInterface* renderPipeline, DrawableProcessor* drawableProcessor,
    BatchStateCacheCallback* callback, DrawableProcessorPassFlags flags, const ea::string& pass)
    : BatchCompositorPass(renderPipeline, drawableProcessor, callback, flags, M_MAX_UNSIGNED,
        Technique::GetPassIndex(pass), M_MAX_UNSIGNED, M_MAX_UNSIGNED)
{
}

BatchRenderFlags UnorderedScenePass::GetDeferredRenderFlags() const
{
    const DrawableProcessorPassFlags passFlags = GetFlags();
    BatchRenderFlags result;
    if (passFlags.Test(DrawableProcessorPassFlag::HasAmbientLighting))
        result |= BatchRenderFlag::EnableAmbientLighting;
    if (HasLightPass())
        result |= BatchRenderFlag::EnablePixelLights;
    if (!passFlags.Test(DrawableProcessorPassFlag::DisableInstancing))
        result |= BatchRenderFlag::EnableInstancingForStaticGeometry;
    return result;
}

BatchRenderFlags UnorderedScenePass::GetBaseRenderFlags() const
{
    const DrawableProcessorPassFlags passFlags = GetFlags();
    BatchRenderFlags result;
    if (passFlags.Test(DrawableProcessorPassFlag::HasAmbientLighting))
        result |= BatchRenderFlag::EnableAmbientAndVertexLighting;
    if (HasLightPass())
        result |= BatchRenderFlag::EnablePixelLights;
    if (!passFlags.Test(DrawableProcessorPassFlag::DisableInstancing))
        result |= BatchRenderFlag::EnableInstancingForStaticGeometry;
    return result;
}

BatchRenderFlags UnorderedScenePass::GetLightRenderFlags() const
{
    const DrawableProcessorPassFlags passFlags = GetFlags();
    BatchRenderFlags result = BatchRenderFlag::EnablePixelLights;
    if (!passFlags.Test(DrawableProcessorPassFlag::DisableInstancing))
        result |= BatchRenderFlag::EnableInstancingForStaticGeometry;
    return result;
}

void UnorderedScenePass::OnBatchesReady()
{
    BatchCompositor::SortBatches(sortedDeferredBatches_, deferredBatches_);
    BatchCompositor::SortBatches(sortedBaseBatches_, baseBatches_);
    BatchCompositor::SortBatches(sortedLightBatches_, lightBatches_);
}

BatchRenderFlags BackToFrontScenePass::GetRenderFlags() const
{
    const DrawableProcessorPassFlags passFlags = GetFlags();
    BatchRenderFlags result;
    if (passFlags.Test(DrawableProcessorPassFlag::HasAmbientLighting))
        result |= BatchRenderFlag::EnableAmbientAndVertexLighting;
    if (HasLightPass())
        result |= BatchRenderFlag::EnablePixelLights;
    if (!passFlags.Test(DrawableProcessorPassFlag::DisableInstancing))
        result |= BatchRenderFlag::EnableInstancingForStaticGeometry;
    return result;
}

void BackToFrontScenePass::OnBatchesReady()
{
    BatchCompositor::SortBatches(sortedBatches_, baseBatches_, lightBatches_);
    if (GetFlags().Test(DrawableProcessorPassFlag::RefractionPass))
    {
        hasRefractionBatches_ = false;
        for (const PipelineBatchBackToFront& sortedBatch : sortedBatches_)
        {
            // Assume refraction if blending is disabled
            if (sortedBatch.pipelineBatch_->pipelineState_->GetDesc().blendMode_ == BLEND_REPLACE)
            {
                hasRefractionBatches_ = true;
                break;
            }
        }
    }
}

}
