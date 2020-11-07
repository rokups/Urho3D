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
#include "../Graphics/Camera.h"
#include "../Graphics/ConstantBuffer.h"
#include "../Graphics/ConstantBufferLayout.h"
#include "../Graphics/RenderPipeline.h"
#include "../Graphics/DrawCommandQueue.h"
#include "../Graphics/IndexBuffer.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/Viewport.h"
#include "../Graphics/RenderPipelineViewport.h"
#include "../Graphics/SceneBatchCollector.h"
#include "../Graphics/SceneBatchRenderer.h"
#include "../Graphics/ShadowMapAllocator.h"
#include "../Scene/Scene.h"

#include <EASTL/fixed_vector.h>
#include <EASTL/vector_map.h>
#include <EASTL/sort.h>

#include "../Graphics/Light.h"
#include "../Graphics/Octree.h"
#include "../Graphics/Geometry.h"
#include "../Graphics/Batch.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/Zone.h"
#include "../Graphics/PipelineState.h"
#include "../Core/WorkQueue.h"

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

CullMode GetEffectiveCullMode(CullMode mode, const Camera* camera)
{
    // If a camera is specified, check whether it reverses culling due to vertical flipping or reflection
    if (camera && camera->GetReverseCulling())
    {
        if (mode == CULL_CW)
            mode = CULL_CCW;
        else if (mode == CULL_CCW)
            mode = CULL_CW;
    }

    return mode;
}

Color GetDefaultFogColor(Graphics* graphics)
{
#ifdef URHO3D_OPENGL
    return graphics->GetForceGL2() ? Color::RED * 0.5f : Color::BLUE * 0.5f;
#else
    return Color::GREEN * 0.5f;
#endif
}

}

RenderPipeline::RenderPipeline(Context* context)
    : Object(context)
    , graphics_(context_->GetSubsystem<Graphics>())
    , renderer_(context_->GetSubsystem<Renderer>())
    , workQueue_(context_->GetSubsystem<WorkQueue>())
{}

RenderPipeline::~RenderPipeline()
{
}

SharedPtr<PipelineState> RenderPipeline::CreatePipelineState(
    const ScenePipelineStateKey& key, const ScenePipelineStateContext& ctx)
{
    Geometry* geometry = key.geometry_;
    Material* material = key.material_;
    Pass* pass = key.pass_;
    Light* light = ctx.light_ ? ctx.light_->GetLight() : nullptr;

    PipelineStateDesc desc;
    ea::string commonDefines;
    ea::string vertexShaderDefines;
    ea::string pixelShaderDefines;

    // Update vertex elements
    for (VertexBuffer* vertexBuffer : geometry->GetVertexBuffers())
    {
        const auto& elements = vertexBuffer->GetElements();
        desc.vertexElements_.append(elements);
    }
    if (desc.vertexElements_.empty())
        return nullptr;

    // Update shadow parameters
    if (ctx.shadowPass_)
        shadowMapAllocator_->ExportPipelineState(desc, light->GetShadowBias());

    // Add vertex input layout defines
    for (const VertexElement& element : desc.vertexElements_)
    {
        if (element.index_ != 0)
        {
            if (element.semantic_)
                vertexShaderDefines += "LAYOUT_HAS_TEXCOORD1 ";
        }
        else
        {
            switch (element.semantic_)
            {
            case SEM_POSITION:
                vertexShaderDefines += "LAYOUT_HAS_POSITION ";
                break;

            case SEM_NORMAL:
                vertexShaderDefines += "LAYOUT_HAS_NORMAL ";
                break;

            case SEM_COLOR:
                vertexShaderDefines += "LAYOUT_HAS_COLOR ";
                break;

            case SEM_TEXCOORD:
                vertexShaderDefines += "LAYOUT_HAS_TEXCOORD0 ";
                break;

            case SEM_TANGENT:
                vertexShaderDefines += "LAYOUT_HAS_TANGENT ";
                break;

            default:
                break;
            }
        }
    }

    // Add geometry type defines
    switch (key.geometryType_)
    {
    case GEOM_STATIC:
    case GEOM_STATIC_NOINSTANCING:
        vertexShaderDefines += "GEOM_STATIC ";
        break;
    case GEOM_INSTANCED:
        vertexShaderDefines += "GEOM_INSTANCED ";
        break;
    case GEOM_SKINNED:
        vertexShaderDefines += "GEOM_SKINNED ";
        break;
    case GEOM_BILLBOARD:
        vertexShaderDefines += "GEOM_BILLBOARD ";
        break;
    case GEOM_DIRBILLBOARD:
        vertexShaderDefines += "GEOM_DIRBILLBOARD ";
        break;
    case GEOM_TRAIL_FACE_CAMERA:
        vertexShaderDefines += "GEOM_TRAIL_FACE_CAMERA ";
        break;
    case GEOM_TRAIL_BONE:
        vertexShaderDefines += "GEOM_TRAIL_BONE ";
        break;
    default:
        break;
    }

    commonDefines += ctx.shaderDefines_;

    if (light)
    {
        commonDefines += "PERPIXEL ";
        if (ctx.light_->HasShadow())
        {
            commonDefines += "SHADOW SIMPLE_SHADOW ";
        }
        switch (light->GetLightType())
        {
        case LIGHT_DIRECTIONAL:
            commonDefines += "DIRLIGHT NUMVERTEXLIGHTS=4 ";
            break;
        case LIGHT_POINT:
            commonDefines += "POINTLIGHT ";
            break;
        case LIGHT_SPOT:
            commonDefines += "SPOTLIGHT ";
            break;
        }
    }
    if (graphics_->GetConstantBuffersEnabled())
        commonDefines += "URHO3D_USE_CBUFFERS ";

    vertexShaderDefines += commonDefines;
    vertexShaderDefines += pass->GetEffectiveVertexShaderDefines();
    pixelShaderDefines += commonDefines;
    pixelShaderDefines += pass->GetEffectivePixelShaderDefines();
    desc.vertexShader_ = graphics_->GetShader(VS, "v2/" + pass->GetVertexShader(), vertexShaderDefines);
    desc.pixelShader_ = graphics_->GetShader(PS, "v2/" + pass->GetPixelShader(), pixelShaderDefines);

    desc.primitiveType_ = geometry->GetPrimitiveType();
    desc.indexType_ = IndexBuffer::GetIndexBufferType(geometry->GetIndexBuffer());

    desc.depthWrite_ = pass->GetDepthWrite();
    desc.depthMode_ = pass->GetDepthTestMode();
    desc.stencilEnabled_ = false;
    desc.stencilMode_ = CMP_ALWAYS;

    desc.colorWrite_ = true;
    desc.blendMode_ = pass->GetBlendMode();
    desc.alphaToCoverage_ = pass->GetAlphaToCoverage();

    desc.fillMode_ = FILL_SOLID;
    const CullMode passCullMode = pass->GetCullMode();
    const CullMode materialCullMode = ctx.shadowPass_ ? material->GetShadowCullMode() : material->GetCullMode();
    const CullMode cullMode = passCullMode != MAX_CULLMODES ? passCullMode : materialCullMode;
    desc.cullMode_ = ctx.shadowPass_ ? cullMode : GetEffectiveCullMode(cullMode, ctx.camera_);

    return renderer_->GetOrCreatePipelineState(desc);
}

SharedPtr<PipelineState> RenderPipeline::CreateLightVolumePipelineState(SceneLight* sceneLight, Geometry* lightGeometry)
{
    ea::string vertexDefines;
    ea::string pixelDefiles = "HWDEPTH ";

    if (graphics_->GetConstantBuffersEnabled())
    {
        vertexDefines += "URHO3D_USE_CBUFFERS ";
        pixelDefiles += "URHO3D_USE_CBUFFERS ";
    }

    Light* light = sceneLight->GetLight();
    LightType type = light->GetLightType();
    switch (type)
    {
    case LIGHT_DIRECTIONAL:
        vertexDefines += "DIRLIGHT ";
        pixelDefiles += "DIRLIGHT ";
        break;

    case LIGHT_SPOT:
        pixelDefiles += "SPOTLIGHT ";
        break;

    case LIGHT_POINT:
        pixelDefiles += "POINTLIGHT ";
        if (light->GetShapeTexture())
            pixelDefiles += "CUBEMASK ";
        break;
    }

    if (sceneLight->GetNumSplits() > 0)
    {
        pixelDefiles += "SHADOW SIMPLE_SHADOW ";
        if (light->GetShadowBias().normalOffset_ > 0.0)
            pixelDefiles += "NORMALOFFSET ";
    }

    if (light->GetSpecularIntensity() > 0.0f)
        pixelDefiles += "SPECULAR ";

    // TODO(renderer): Fix me
    /*if (camera->IsOrthographic())
    {
        vertexDefines += DLVS_ORTHO;
        pixelDefiles += DLPS_ORTHO;
    }*/

    PipelineStateDesc desc;
    desc.vertexElements_ = lightGeometry->GetVertexBuffer(0)->GetElements();

    desc.primitiveType_ = lightGeometry->GetPrimitiveType();
    desc.indexType_ = IndexBuffer::GetIndexBufferType(lightGeometry->GetIndexBuffer());
    desc.stencilEnabled_ = false;
    desc.stencilMode_ = CMP_ALWAYS;

    desc.colorWrite_ = true;
    desc.blendMode_ = light->IsNegative() ? BLEND_SUBTRACT : BLEND_ADD;
    desc.alphaToCoverage_ = false;

    desc.fillMode_ = FILL_SOLID;

    // TODO(renderer): Extract this code to collector
    Vector3 cameraPos = camera_->GetNode()->GetWorldPosition();
    if (type != LIGHT_DIRECTIONAL)
    {
        float lightDist{};
        if (type == LIGHT_POINT)
            lightDist = Sphere(light->GetNode()->GetWorldPosition(), light->GetRange() * 1.25f).Distance(cameraPos);
        else
            lightDist = light->GetFrustum().Distance(cameraPos);

        // Draw front faces if not inside light volume
        if (lightDist < camera_->GetNearClip() * 2.0f)
        {
            desc.cullMode_ = GetEffectiveCullMode(CULL_CW, camera_);
            desc.depthMode_ = CMP_GREATER;
        }
        else
        {
            desc.cullMode_ = GetEffectiveCullMode(CULL_CCW, camera_);
            desc.depthMode_ = CMP_LESSEQUAL;
        }
    }
    else
    {
        desc.cullMode_ = CULL_NONE;
        desc.depthMode_ = CMP_ALWAYS;
    }

    desc.vertexShader_ = graphics_->GetShader(VS, "v2/DeferredLight", vertexDefines);
    desc.pixelShader_ = graphics_->GetShader(PS, "v2/DeferredLight", pixelDefiles);

    return renderer_->GetOrCreatePipelineState(desc);
}

bool RenderPipeline::HasShadow(Light* light)
{
    const bool shadowsEnabled = renderer_->GetDrawShadows()
        && light->GetCastShadows()
        && light->GetLightImportance() != LI_NOT_IMPORTANT
        && light->GetShadowIntensity() < 1.0f;

    if (!shadowsEnabled)
        return false;

    if (light->GetShadowDistance() > 0.0f && light->GetDistance() > light->GetShadowDistance())
        return false;

    // OpenGL ES can not support point light shadows
#ifdef GL_ES_VERSION_2_0
    if (light->GetLightType() == LIGHT_POINT)
        return false;
#endif

    return true;
}

ShadowMap RenderPipeline::GetTemporaryShadowMap(const IntVector2& size)
{
    return shadowMapAllocator_->AllocateShadowMap(size);
}

bool RenderPipeline::Define(RenderSurface* renderTarget, Viewport* viewport)
{
    scene_ = viewport->GetScene();
    camera_ = scene_ ? viewport->GetCamera() : nullptr;
    octree_ = scene_ ? scene_->GetComponent<Octree>() : nullptr;
    if (!camera_ || !octree_)
        return false;

    numDrawables_ = octree_->GetAllDrawables().size();

    viewport_ = MakeShared<RenderPipelineViewport>(context_);
    viewport_->Define(renderTarget, viewport);
    shadowMapAllocator_ = MakeShared<ShadowMapAllocator>(context_);

    viewport_->AddRenderTarget("viewport", "rgba");
    viewport_->AddRenderTarget("albedo", "rgba");
    viewport_->AddRenderTarget("normal", "rgba");
    viewport_->AddRenderTarget("depth", "readabledepth");

    return true;
}

void RenderPipeline::Update(const FrameInfo& frameInfo)
{
    frameInfo_ = frameInfo;
    frameInfo_.camera_ = camera_;
    frameInfo_.octree_ = octree_;
    numThreads_ = workQueue_->GetNumThreads() + 1;
}

void RenderPipeline::PostTask(std::function<void(unsigned)> task)
{
    workQueue_->AddWorkItem(ea::move(task), M_MAX_UNSIGNED);
}

void RenderPipeline::CompleteTasks()
{
    workQueue_->Complete(M_MAX_UNSIGNED);
}

void RenderPipeline::CollectDrawables(ea::vector<Drawable*>& drawables, Camera* camera, DrawableFlags flags)
{
    FrustumOctreeQuery query(drawables, camera->GetFrustum(), flags, camera->GetViewMask());
    octree_->GetDrawables(query);
}

void RenderPipeline::Render()
{
    viewport_->BeginFrame();
    shadowMapAllocator_->Reset();

    // Set automatic aspect ratio if required
    if (camera_ && camera_->GetAutoAspectRatio())
        camera_->SetAspectRatioInternal((float)frameInfo_.viewSize_.x_ / (float)frameInfo_.viewSize_.y_);

    // Collect and process visible drawables
    static ea::vector<Drawable*> drawablesInMainCamera;
    drawablesInMainCamera.clear();
    CollectDrawables(drawablesInMainCamera, camera_, DRAWABLE_GEOMETRY | DRAWABLE_LIGHT);

    // Process batches
    static SceneBatchCollector sceneBatchCollector(context_);
    static auto sceneBatchRenderer = MakeShared<SceneBatchRenderer>(context_);
    /*static ScenePassDescription passes[] = {
        { ScenePassType::ForwardLitBase,   "base",  "litbase",  "light" },
        { ScenePassType::ForwardUnlitBase, "alpha", "",         "litalpha" },
        { ScenePassType::Unlit, "postopaque" },
        { ScenePassType::Unlit, "refract" },
        { ScenePassType::Unlit, "postalpha" },
    };*/
    sceneBatchCollector.SetMaxPixelLights(4);

    static auto basePass = MakeShared<OpaqueForwardLightingScenePass>(context_, "PASS_BASE", "base", "litbase", "light");
    static auto alphaPass = MakeShared<AlphaForwardLightingScenePass>(context_, "PASS_ALPHA", "alpha", "alpha", "litalpha");
    static auto deferredPass = MakeShared<UnlitScenePass>(context_, "PASS_DEFERRED", "deferred");
    static auto shadowPass = MakeShared<ShadowScenePass>(context_, "PASS_SHADOW", "shadow");
    sceneBatchCollector.ResetPasses();
    sceneBatchCollector.SetShadowPass(shadowPass);
    sceneBatchCollector.AddScenePass(basePass);
    sceneBatchCollector.AddScenePass(alphaPass);
    sceneBatchCollector.AddScenePass(deferredPass);

    sceneBatchCollector.BeginFrame(frameInfo_, *this);
    sceneBatchCollector.ProcessVisibleDrawables(drawablesInMainCamera);
    sceneBatchCollector.ProcessVisibleLights();
    sceneBatchCollector.UpdateGeometries();
    sceneBatchCollector.CollectSceneBatches();
    sceneBatchCollector.CollectLightVolumeBatches();

    // Collect batches
    static DrawCommandQueue drawQueue;
    const auto zone = octree_->GetZone();

    const auto& visibleLights = sceneBatchCollector.GetVisibleLights();
    for (SceneLight* sceneLight : visibleLights)
    {
        const unsigned numSplits = sceneLight->GetNumSplits();
        for (unsigned splitIndex = 0; splitIndex < numSplits; ++splitIndex)
        {
            const SceneLightShadowSplit& split = sceneLight->GetSplit(splitIndex);
            const auto& shadowBatches = shadowPass->GetSortedShadowBatches(split);

            drawQueue.Reset(graphics_);
            sceneBatchRenderer->RenderShadowBatches(drawQueue, sceneBatchCollector, split.shadowCamera_, zone, shadowBatches);
            shadowMapAllocator_->BeginShadowMap(split.shadowMap_);
            drawQueue.Execute(graphics_);
        }
    }

    // TODO(renderer): Remove this guard
#ifdef DESKTOP_GRAPHICS
    // Draw deferred GBuffer
    viewport_->ClearRenderTarget("viewport", GetDefaultFogColor(graphics_));
    viewport_->ClearRenderTarget("albedo", Color::TRANSPARENT_BLACK);
    viewport_->ClearDepthStencil("depth", 1.0f, 0);
    const ea::string_view geometryBufferTargets[] = { "viewport", "albedo", "normal" };
    viewport_->SetRenderTargets("depth", geometryBufferTargets);
    drawQueue.Reset(graphics_);
    sceneBatchRenderer->RenderUnlitBaseBatches(drawQueue, sceneBatchCollector, camera_, zone, deferredPass->GetBatches());
    drawQueue.Execute(graphics_);

    // Draw deferred lights
    GeometryBufferResource geometryBuffer[] = {
        { TU_ALBEDOBUFFER, viewport_->GetRenderTarget("albedo") },
        { TU_NORMALBUFFER, viewport_->GetRenderTarget("normal") },
        { TU_DEPTHBUFFER, viewport_->GetRenderTarget("depth") }
    };

    const IntVector2 gbufferSize = geometryBuffer[0].texture_->GetSize();
    const IntRect gbufferRect{ IntVector2::ZERO, gbufferSize };

    drawQueue.Reset(graphics_);
    sceneBatchRenderer->RenderLightVolumeBatches(drawQueue, sceneBatchCollector, camera_, zone,
        sceneBatchCollector.GetLightVolumeBatches(), geometryBuffer,
        viewport_->GetGBufferOffsets(gbufferSize, gbufferRect), viewport_->GetGBufferInvSize(gbufferSize));

    const ea::string_view viewportTarget[] = { "viewport" };
    viewport_->SetRenderTargets("depth", viewportTarget);
    drawQueue.Execute(graphics_);

    // Draw forward
    viewport_->SetViewportRenderTargets(CLEAR_COLOR | CLEAR_DEPTH | CLEAR_STENCIL, GetDefaultFogColor(graphics_), 1.0f, 0);
    drawQueue.Reset(graphics_);
    sceneBatchRenderer->RenderUnlitBaseBatches(drawQueue, sceneBatchCollector, camera_, zone, basePass->GetSortedUnlitBaseBatches());
    sceneBatchRenderer->RenderLitBaseBatches(drawQueue, sceneBatchCollector, camera_, zone, basePass->GetSortedLitBaseBatches());
    sceneBatchRenderer->RenderLightBatches(drawQueue, sceneBatchCollector, camera_, zone, basePass->GetSortedLightBatches());
    sceneBatchRenderer->RenderAlphaBatches(drawQueue, sceneBatchCollector, camera_, zone, alphaPass->GetSortedBatches());
    drawQueue.Execute(graphics_);

    // Post-process
    static unsigned frame = 0;
    ++frame;
    if (frame % 100 < 50)
        viewport_->CopyToViewportRenderTarget("viewport");
#endif

    viewport_->EndFrame();
}

}
