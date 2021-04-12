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

#include "../Core/StringUtils.h"
#include "../Graphics/Drawable.h"
#include "../Graphics/Geometry.h"
#include "../Graphics/Light.h"
#include "../Graphics/Material.h"
#include "../Graphics/PipelineState.h"
#include "../RenderPipeline/BatchCompositor.h"
#include "../RenderPipeline/RenderPipelineDebugger.h"

#include "../DebugNew.h"

namespace Urho3D
{

DebugFrameSnapshotBatch::DebugFrameSnapshotBatch(const DrawableProcessor& drawableProcessor,
    const PipelineBatch& pipelineBatch, bool newInstancingGroup)
    : drawable_(pipelineBatch.drawable_)
    , geometry_(pipelineBatch.geometry_)
    , material_(pipelineBatch.material_)
    , pipelineState_(pipelineBatch.pipelineState_)
    , sourceBatchIndex_(pipelineBatch.sourceBatchIndex_)
    , distance_(pipelineBatch.distance_)
    , numVertices_(geometry_->GetVertexCount())
    , numPrimitives_(geometry_->GetPrimitiveCount())
    , newInstancingGroup_(newInstancingGroup)
{
    const ea::vector<Light*>& lights = drawableProcessor.GetLights();
    light_ = pipelineBatch.pixelLightIndex_ < lights.size() ? lights[pipelineBatch.pixelLightIndex_] : nullptr;
}

ea::string DebugFrameSnapshotBatch::ToString() const
{
    const ea::string drawableName = drawable_ && sourceBatchIndex_ != M_MAX_UNSIGNED ? drawable_->GetFullNameDebug() : "";
    const ea::string lightName = light_ ? light_->GetFullNameDebug() : "null";
    const ea::string materialName = material_ ? material_->GetName() : "null";

    const char bulletPoint = newInstancingGroup_ ? '*' : '.';
    const ea::string geometryText = !drawableName.empty()
        ? Format("[{}].{} with material [{}] lit with", drawableName, sourceBatchIndex_, materialName)
        : "Light volume geometry for";

    return Format("{} {}v {}t {} [{}] (distance={:.2f} state={} geometry={})",
        bulletPoint, numVertices_, numPrimitives_, geometryText, lightName,
        distance_, static_cast<void*>(pipelineState_), static_cast<void*>(geometry_));
}

ea::string DebugFrameSnapshotPass::ToString() const
{
    unsigned numVertices = 0;
    unsigned numPrimitives = 0;
    ea::string batches;
    for (const DebugFrameSnapshotBatch& batch : batches_)
    {
        batches += batch.ToString();
        batches += "\n";
        numVertices += batch.numVertices_;
        numPrimitives += batch.numPrimitives_;
    }
    return Format("Pass {} - {}b {}v {}t:\n\n{}\n", name_, batches_.size(), numVertices, numPrimitives, batches);
}

ea::string DebugFrameSnapshot::ToString() const
{
    ea::string result;
    for (const DebugFrameSnapshotPass& pass : passes_)
        result += pass.ToString();
    return result;
}

void RenderPipelineDebugger::BeginSnapshot()
{
    snapshotBuildingInProgress_ = true;
    snapshot_ = {};
}

void RenderPipelineDebugger::EndSnapshot()
{
    snapshotBuildingInProgress_ = false;
}

void RenderPipelineDebugger::BeginPass(ea::string_view name)
{
    if (passInProgress_)
        EndPass();
    snapshot_.passes_.push_back(DebugFrameSnapshotPass{ ea::string(name) });
    passInProgress_ = true;
}

void RenderPipelineDebugger::ReportSceneBatch(const DebugFrameSnapshotBatch& sceneBatch)
{
    if (!passInProgress_)
        BeginPass("Unnamed");
    snapshot_.passes_.back().batches_.push_back(sceneBatch);
}

void RenderPipelineDebugger::EndPass()
{
    passInProgress_ = false;
}

}
