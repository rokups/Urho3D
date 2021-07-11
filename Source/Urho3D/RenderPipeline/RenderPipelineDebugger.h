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

#pragma once

#include "../RenderPipeline/RenderPipelineDefs.h"

#include <EASTL/string.h>
#include <EASTL/unordered_set.h>
#include <EASTL/vector.h>

namespace Urho3D
{

class Drawable;
class DrawableProcessor;
class Geometry;
class Light;
class Material;
class PipelineState;
class ShaderVariation;
struct PipelineBatch;

struct URHO3D_API DebugFrameSnapshotBatch
{
    Drawable* drawable_{};
    Geometry* geometry_{};
    Material* material_{};
    Light* light_{};
    PipelineState* pipelineState_{};
    unsigned sourceBatchIndex_{};
    float distance_{};
    unsigned numVertices_{};
    unsigned numPrimitives_{};
    bool newInstancingGroup_{};

    DebugFrameSnapshotBatch() = default;
    DebugFrameSnapshotBatch(const DrawableProcessor& drawableProcessor,
        const PipelineBatch& pipelineBatch, bool newInstancingGroup);
    ea::string ToString() const;
};

struct URHO3D_API DebugFrameSnapshotQuad
{
    ea::string debugComment_;
    IntVector2 size_;

    ea::string ToString() const;
};

struct URHO3D_API DebugFrameSnapshotPass
{
    ea::string name_;
    ea::vector<DebugFrameSnapshotBatch> batches_;
    ea::vector<DebugFrameSnapshotQuad> quads_;

    ea::string ToString() const;
};

struct URHO3D_API DebugFrameSnapshot
{
    ea::vector<DebugFrameSnapshotPass> passes_;
    ea::unordered_set<PipelineState*> scenePipelineStates_{};
    ea::unordered_set<Material*> sceneMaterials_{};
    ea::unordered_set<ShaderVariation*> sceneShaders_{};

    ea::string ToString() const;
    ea::string ScenePipelineStatesToString() const;
    ea::string SceneMaterialsToString() const;
    ea::string SceneShadersToString() const;
};;

/// Debug utility that takes snapshot of current frame.
class URHO3D_API RenderPipelineDebugger
{
public:
    RenderPipelineDebugger() {}

    /// Flow control
    /// @{
    void BeginSnapshot();
    void EndSnapshot();
    /// @}

    /// Debug info reporting. Should be called from main thread.
    /// @{
    void BeginPass(ea::string_view name);
    void ReportSceneBatch(const DebugFrameSnapshotBatch& sceneBatch);
    void ReportQuad(ea::string_view debugComment, const IntVector2& size = IntVector2::ZERO);
    void EndPass();
    /// @}

    bool IsSnapshotInProgress() const { return snapshotBuildingInProgress_; }
    static bool IsSnapshotInProgress(const RenderPipelineDebugger* debugger) { return debugger && debugger->IsSnapshotInProgress(); }
    const DebugFrameSnapshot& GetSnapshot() const { return snapshot_; }

private:
    bool snapshotBuildingInProgress_{};
    DebugFrameSnapshot snapshot_;

    bool passInProgress_{};
};

}
