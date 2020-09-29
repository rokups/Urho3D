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
#include "../Graphics/CustomView.h"
#include "../Graphics/DrawCommandQueue.h"
#include "../Graphics/IndexBuffer.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/Viewport.h"
#include "../Graphics/SceneBatchCollector.h"
#include "../Graphics/SceneBatchRenderer.h"
#include "../Graphics/SceneViewport.h"
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

class TestFactory : public SceneBatchCollectorCallback, public Object
{
    URHO3D_OBJECT(TestFactory, Object);

public:
    explicit TestFactory(Context* context, ShadowMapAllocator* shadowMapAllocator)
        : Object(context)
        , graphics_(context->GetGraphics())
        , renderer_(context->GetRenderer())
        , shadowMapAllocator_(shadowMapAllocator)
    {}

    SharedPtr<PipelineState> CreatePipelineState(
        const ScenePipelineStateKey& key, const ScenePipelineStateContext& ctx) override
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

    bool HasShadow(Light* light) override
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

    ShadowMap GetTemporaryShadowMap(const IntVector2& size) override
    {
        return shadowMapAllocator_->AllocateShadowMap(size);
    }

private:
    static CullMode GetEffectiveCullMode(CullMode mode, const Camera* camera)
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

    Graphics* graphics_{};
    Renderer* renderer_{};
    ShadowMapAllocator* shadowMapAllocator_{};
};

}

CustomView::CustomView(Context* context, CustomViewportScript* script)
    : Object(context)
    , graphics_(context_->GetGraphics())
    , workQueue_(context_->GetWorkQueue())
    , script_(script)
{}

CustomView::~CustomView()
{
}

bool CustomView::Define(RenderSurface* renderTarget, Viewport* viewport)
{
    scene_ = viewport->GetScene();
    camera_ = scene_ ? viewport->GetCamera() : nullptr;
    octree_ = scene_ ? scene_->GetComponent<Octree>() : nullptr;
    if (!camera_ || !octree_)
        return false;

    numDrawables_ = octree_->GetAllDrawables().size();
    renderTarget_ = renderTarget;
    viewport_ = viewport;
    return true;
}

void CustomView::Update(const FrameInfo& frameInfo)
{
    frameInfo_ = frameInfo;
    frameInfo_.camera_ = camera_;
    frameInfo_.octree_ = octree_;
    numThreads_ = workQueue_->GetNumThreads() + 1;
}

void CustomView::PostTask(std::function<void(unsigned)> task)
{
    workQueue_->AddWorkItem(ea::move(task), M_MAX_UNSIGNED);
}

void CustomView::CompleteTasks()
{
    workQueue_->Complete(M_MAX_UNSIGNED);
}

/*void CustomView::ClearViewport(ClearTargetFlags flags, const Color& color, float depth, unsigned stencil)
{
    graphics_->Clear(flags, color, depth, stencil);
}*/

void CustomView::CollectDrawables(ea::vector<Drawable*>& drawables, Camera* camera, DrawableFlags flags)
{
    FrustumOctreeQuery query(drawables, camera->GetFrustum(), flags, camera->GetViewMask());
    octree_->GetDrawables(query);
}

void CustomView::Render()
{
    static auto sceneViewport = MakeShared<SceneViewport>(context_);
    sceneViewport->BeginFrame(renderTarget_, viewport_);

    static auto shadowMapAllocator = MakeShared<ShadowMapAllocator>(context_);
    shadowMapAllocator->Reset();

    //script_->Render(this);

    // Set automatic aspect ratio if required
    if (camera_ && camera_->GetAutoAspectRatio())
        camera_->SetAspectRatioInternal((float)frameInfo_.viewSize_.x_ / (float)frameInfo_.viewSize_.y_);

    // Collect and process visible drawables
    static ea::vector<Drawable*> drawablesInMainCamera;
    drawablesInMainCamera.clear();
    CollectDrawables(drawablesInMainCamera, camera_, DRAWABLE_GEOMETRY | DRAWABLE_LIGHT);

    // Process batches
    static TestFactory scenePipelineStateFactory(context_, shadowMapAllocator);
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
    static auto shadowPass = MakeShared<ShadowScenePass>(context_, "PASS_SHADOW", "shadow");
    sceneBatchCollector.ResetPasses();
    sceneBatchCollector.SetShadowPass(shadowPass);
    sceneBatchCollector.AddScenePass(basePass);
    sceneBatchCollector.AddScenePass(alphaPass);

    sceneBatchCollector.BeginFrame(frameInfo_, scenePipelineStateFactory);
    sceneBatchCollector.ProcessVisibleDrawables(drawablesInMainCamera);
    sceneBatchCollector.ProcessVisibleLights();
    sceneBatchCollector.UpdateGeometries();
    sceneBatchCollector.CollectSceneBatches();

    // Collect batches
    static DrawCommandQueue drawQueue;
    const auto zone = octree_->GetZone();

    auto renderer = context_->GetRenderer();
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
            shadowMapAllocator->BeginShadowMap(split.shadowMap_);
            drawQueue.Execute(graphics_);
        }
    }

    sceneViewport->SetOutputRenderTarget();
#ifdef URHO3D_OPENGL
    graphics_->Clear(CLEAR_COLOR | CLEAR_DEPTH | CLEAR_STENCIL, Color::RED * 0.5f);
#else
    graphics_->Clear(CLEAR_COLOR | CLEAR_DEPTH | CLEAR_STENCIL, Color::GREEN * 0.5f);
#endif

    drawQueue.Reset(graphics_);

    sceneBatchRenderer->RenderUnlitBaseBatches(drawQueue, sceneBatchCollector, camera_, zone, basePass->GetSortedUnlitBaseBatches());
    sceneBatchRenderer->RenderLitBaseBatches(drawQueue, sceneBatchCollector, camera_, zone, basePass->GetSortedLitBaseBatches());
    sceneBatchRenderer->RenderLightBatches(drawQueue, sceneBatchCollector, camera_, zone, basePass->GetSortedLightBatches());
    sceneBatchRenderer->RenderAlphaBatches(drawQueue, sceneBatchCollector, camera_, zone, alphaPass->GetSortedBatches());

    drawQueue.Execute(graphics_);

    sceneViewport->EndFrame();
}

}
