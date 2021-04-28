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
#include "../Graphics/ShaderVariation.h"
#include "../RenderPipeline/BatchCompositor.h"
#include "../RenderPipeline/RenderPipelineDebugger.h"

#include "../DebugNew.h"

#include <EASTL/tuple.h>

namespace Urho3D
{

namespace
{

ea::tuple<ea::string, ea::string> MakeSortKey(const PipelineState& pipelineState)
{
    const PipelineStateDesc& desc = pipelineState.GetDesc();
    return { desc.vertexShader_->GetName(), desc.pixelShader_->GetName() };
}

ea::tuple<ea::string> MakeSortKey(const Material& material)
{
    return { material.GetName() };
}

ea::tuple<ShaderType, ea::string, ea::string> MakeSortKey(const ShaderVariation& shader)
{
    return { shader.GetShaderType(), shader.GetName(), shader.GetDefines() };
}

template <class T>
ea::vector<T*> GetSortedObjects(const ea::unordered_set<T*>& set)
{
    ea::vector<T*> vector(set.begin(), set.end());
    ea::sort(vector.begin(), vector.end(), [](T* lhs, T* rhs)
    {
        return MakeSortKey(*lhs) < MakeSortKey(*rhs);
    });
    return vector;
}

}

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
    const ea::string detailsText = Format("distance={:.2f} state={} geometry={} material={}",
        distance_, static_cast<void*>(pipelineState_), static_cast<void*>(geometry_), static_cast<void*>(material_));

    return Format("{} {}v {}t {} [{}] ({})",
        bulletPoint, numVertices_, numPrimitives_, geometryText, lightName, detailsText);
}

ea::string DebugFrameSnapshotQuad::ToString() const
{
    const ea::string sizeText = size_ != IntVector2::ZERO ? Format(" {}x{}", size_.x_, size_.y_) : "";
    return Format("+ [quad{}] {}", sizeText, debugComment_);
}

ea::string DebugFrameSnapshotPass::ToString() const
{
    unsigned numBatches = 0;
    unsigned numVertices = 0;
    unsigned numPrimitives = 0;
    ea::string result;

    numBatches += batches_.size();
    for (const DebugFrameSnapshotBatch& batch : batches_)
    {
        result += batch.ToString();
        result += "\n";
        numVertices += batch.numVertices_;
        numPrimitives += batch.numPrimitives_;
    }

    numBatches += quads_.size();
    for (const DebugFrameSnapshotQuad& quad : quads_)
    {
        result += quad.ToString();
        result += "\n";
        numVertices += 4;
        numPrimitives += 2;
    }
    return Format("Pass {} - {}b {}v {}t:\n\n{}\n", name_, numBatches, numVertices, numPrimitives, result);
}

ea::string DebugFrameSnapshot::ToString() const
{
    ea::string result;
    for (const DebugFrameSnapshotPass& pass : passes_)
        result += pass.ToString();
    result += Format("Pipeline states in scene ({}): \n\n{}\n", scenePipelineStates_.size(), ScenePipelineStatesToString());
    result += Format("Materials in scene ({}): \n\n{}\n", sceneMaterials_.size(), SceneMaterialsToString());
    result += Format("Shaders in scene ({}): \n\n{}\n", sceneShaders_.size(), SceneShadersToString());
    return result;
}

ea::string DebugFrameSnapshot::ScenePipelineStatesToString() const
{
    ea::string result;
    for (PipelineState* pipelineState : GetSortedObjects(scenePipelineStates_))
    {
        const PipelineStateDesc& desc = pipelineState->GetDesc();
        result += Format("- {}: VS={} PS={}\n", static_cast<void*>(pipelineState),
            static_cast<void*>(desc.vertexShader_), static_cast<void*>(desc.pixelShader_));
    }
    return result;
}

ea::string DebugFrameSnapshot::SceneMaterialsToString() const
{
    ea::string result;
    for (Material* material : GetSortedObjects(sceneMaterials_))
    {
        const ea::string name = !material->GetName().empty() ? material->GetName() : "Unnamed";
        result += Format("- {}: {}\n", static_cast<void*>(material), name);
    }
    return result;
}

ea::string DebugFrameSnapshot::SceneShadersToString() const
{
    static const ea::string shaderTypes[] = { "VS", "PS" };

    ea::string result;
    for (ShaderVariation* shader : GetSortedObjects(sceneShaders_))
    {
        const ea::string& shaderType = shaderTypes[shader->GetShaderType()];
        result += Format("- {}: [{}]{}: {}\n",
            static_cast<void*>(shader), shaderType, shader->GetName(), shader->GetDefines());
    }
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
    snapshot_.scenePipelineStates_.insert(sceneBatch.pipelineState_);
    snapshot_.sceneMaterials_.insert(sceneBatch.material_);

    const PipelineStateDesc& pipelineStateDesc = sceneBatch.pipelineState_->GetDesc();
    snapshot_.sceneShaders_.insert(pipelineStateDesc.vertexShader_);
    snapshot_.sceneShaders_.insert(pipelineStateDesc.pixelShader_);
}

void RenderPipelineDebugger::ReportQuad(ea::string_view debugComment, const IntVector2& size)
{
    if (!passInProgress_)
        BeginPass("Unnamed");
    snapshot_.passes_.back().quads_.push_back(DebugFrameSnapshotQuad{ ea::string(debugComment), size });
}

void RenderPipelineDebugger::EndPass()
{
    passInProgress_ = false;
}

}
