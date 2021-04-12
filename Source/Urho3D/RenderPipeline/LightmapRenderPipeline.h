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

#include "../Core/Object.h"
#include "../RenderPipeline/RenderBuffer.h"
#include "../RenderPipeline/RenderPipelineDefs.h"

namespace Urho3D
{

/// RenderPipeline used to render geometry buffer for lightmap baking.
class URHO3D_API LightmapRenderPipelineView
    : public Object
    , public RenderPipelineInterface
{
    URHO3D_OBJECT(LightmapRenderPipelineView, Object);

public:
    explicit LightmapRenderPipelineView(Context* context);
    ~LightmapRenderPipelineView() override;

    /// Render geometry buffer. May be called only once.
    void RenderGeometryBuffer(Viewport* viewport, int textureSize);

    /// Return geometry buffer textures
    /// @{
    Texture2D* GetPositionBuffer() const { return positionBuffer_->GetTexture2D(); }
    Texture2D* GetSmoothPositionBuffer() const { return smoothPositionBuffer_->GetTexture2D(); }
    Texture2D* GetFaceNormalBuffer() const { return faceNormalBuffer_->GetTexture2D(); }
    Texture2D* GetSmoothNormalBuffer() const { return smoothNormalBuffer_->GetTexture2D(); }
    Texture2D* GetAlbedoBuffer() const { return albedoBuffer_->GetTexture2D(); }
    Texture2D* GetEmissionBuffer() const { return emissionBuffer_->GetTexture2D(); }
    /// @}

    /// Implement RenderPipelineInterface
    /// @{
    Context* GetContext() const override { return BaseClassName::GetContext(); }
    RenderPipelineDebugger* GetDebugger() override { return nullptr; }
    /// @}

private:
    SharedPtr<RenderBuffer> depthBuffer_;
    SharedPtr<RenderBuffer> positionBuffer_;
    SharedPtr<RenderBuffer> smoothPositionBuffer_;
    SharedPtr<RenderBuffer> faceNormalBuffer_;
    SharedPtr<RenderBuffer> smoothNormalBuffer_;
    SharedPtr<RenderBuffer> albedoBuffer_;
    SharedPtr<RenderBuffer> emissionBuffer_;
};

}
