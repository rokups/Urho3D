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

#include "../Graphics/Renderer.h"
#include "../RenderPipeline/BatchRenderer.h"
#include "../RenderPipeline/InstancingBuffer.h"
#include "../RenderPipeline/LightmapRenderPipeline.h"
#include "../RenderPipeline/RenderBufferManager.h"
#include "../RenderPipeline/ScenePass.h"
#include "../RenderPipeline/SceneProcessor.h"
#include "../RenderPipeline/ShadowMapAllocator.h"

#include "../DebugNew.h"

namespace Urho3D
{

LightmapRenderPipelineView::LightmapRenderPipelineView(Context* context)
    : Object(context)
{
}

LightmapRenderPipelineView::~LightmapRenderPipelineView()
{
}

void LightmapRenderPipelineView::RenderGeometryBuffer(Viewport* viewport, int textureSize)
{
#ifdef DESKTOP_GRAPHICS
    auto renderBufferManager = MakeShared<RenderBufferManager>(this);
    auto shadowMapAllocator = MakeShared<ShadowMapAllocator>(context_);
    auto instancingBuffer = MakeShared<InstancingBuffer>(context_);
    auto sceneProcessor = MakeShared<SceneProcessor>(this, "", shadowMapAllocator, instancingBuffer);

    auto pass = sceneProcessor->CreatePass<UnorderedScenePass>(DrawableProcessorPassFlag::None, "deferred", "", "", "");

    const Vector2 size = Vector2::ONE * textureSize;
    const RenderBufferFlags flags = RenderBufferFlag::FixedTextureSize | RenderBufferFlag::Persistent;
    const unsigned colorFormat = Graphics::GetRGBAFloat32Format();
    depthBuffer_ = renderBufferManager->CreateColorBuffer({ Graphics::GetReadableDepthFormat(), 1, flags }, size);
    positionBuffer_ = renderBufferManager->CreateColorBuffer({ colorFormat, 1, flags }, size);
    smoothPositionBuffer_ = renderBufferManager->CreateColorBuffer({ colorFormat, 1, flags }, size);
    faceNormalBuffer_ = renderBufferManager->CreateColorBuffer({ colorFormat, 1, flags }, size);
    smoothNormalBuffer_ = renderBufferManager->CreateColorBuffer({ colorFormat, 1, flags }, size);
    albedoBuffer_ = renderBufferManager->CreateColorBuffer({ colorFormat, 1, flags }, size);
    emissionBuffer_ = renderBufferManager->CreateColorBuffer({ colorFormat, 1, flags }, size);

    // Use main viewport as render target because it's not used anyway
    CommonFrameInfo frameInfo;
    frameInfo.viewport_ = viewport;
    frameInfo.renderTarget_ = nullptr;
    frameInfo.viewportRect_ = viewport->GetEffectiveRect(nullptr);
    frameInfo.viewportSize_ = frameInfo.viewportRect_.Size();

    sceneProcessor->SetPasses({ pass });
    sceneProcessor->Define(frameInfo);
    sceneProcessor->SetRenderCamera(viewport->GetCamera());

    OnUpdateBegin(this, frameInfo);
    sceneProcessor->Update();
    OnUpdateEnd(this, frameInfo);

    OnRenderBegin(this, frameInfo);

    const Color invalidPosition{ -100000000.0f, -100000000.0f, -100000000.0f, 0.0f };
    renderBufferManager->ClearDepthStencil(depthBuffer_, CLEAR_DEPTH, 1.0f, 0);
    renderBufferManager->ClearColor(positionBuffer_, invalidPosition);
    renderBufferManager->ClearColor(smoothPositionBuffer_, invalidPosition);
    renderBufferManager->ClearColor(faceNormalBuffer_, Color::TRANSPARENT_BLACK);
    renderBufferManager->ClearColor(smoothNormalBuffer_, Color::TRANSPARENT_BLACK);
    renderBufferManager->ClearColor(albedoBuffer_, Color::TRANSPARENT_BLACK);
    renderBufferManager->ClearColor(emissionBuffer_, Color::TRANSPARENT_BLACK);

    auto renderer = GetSubsystem<Renderer>();
    DrawCommandQueue* drawQueue = renderer->GetDefaultDrawQueue();
    BatchRenderer* batchRenderer = sceneProcessor->GetBatchRenderer();

    RenderBuffer* const gBuffer[] = {
        positionBuffer_,
        smoothPositionBuffer_,
        faceNormalBuffer_,
        smoothNormalBuffer_,
        albedoBuffer_,
        emissionBuffer_
    };
    renderBufferManager->SetRenderTargets(depthBuffer_, gBuffer);

    drawQueue->Reset();

    instancingBuffer->Begin();
    batchRenderer->RenderBatches({ *drawQueue, *viewport->GetCamera() }, pass->GetDeferredBatches());
    instancingBuffer->End();

    drawQueue->Execute();

    // Do not end the renderer so textures are available outside
    //OnRenderEnd(this, frameInfo);
#endif
}

}
