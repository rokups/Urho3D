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
#include "../Input/Input.h"
#include "../Graphics/Camera.h"
#include "../Graphics/ConstantBuffer.h"
#include "../Graphics/ShaderProgramLayout.h"
#include "../Graphics/DebugRenderer.h"
#include "../Graphics/DrawCommandQueue.h"
#include "../Graphics/IndexBuffer.h"
#include "../Graphics/OcclusionBuffer.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/GraphicsEvents.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/Viewport.h"
#include "../RenderPipeline/RenderPipeline.h"
#include "../RenderPipeline/InstancingBuffer.h"
#include "../RenderPipeline/LightProcessor.h"
#include "../RenderPipeline/DrawableProcessor.h"
#include "../RenderPipeline/BatchRenderer.h"
#include "../RenderPipeline/ShadowMapAllocator.h"
#include "../RenderPipeline/AutoExposurePass.h"
#include "../RenderPipeline/ToneMappingPass.h"
#include "../RenderPipeline/BloomPass.h"
#include "../Scene/Scene.h"
#if URHO3D_SYSTEMUI
    #include "../SystemUI/SystemUI.h"
#endif

#include <EASTL/fixed_vector.h>
#include <EASTL/vector_map.h>
#include <EASTL/sort.h>

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

static const ea::vector<ea::string> colorSpaceNames =
{
    "LDR Gamma",
    "LDR Linear",
    "HDR Linear",
};

static const ea::vector<ea::string> materialQualityNames =
{
    "Low",
    "Medium",
    "High",
};

static const ea::vector<ea::string> ambientModeNames =
{
    "Constant",
    "Flat",
    "Directional",
};

static const ea::vector<ea::string> directLightingModeNames =
{
    "Forward",
    "Deferred Blinn-Phong",
    "Deferred PBR",
};

static const ea::vector<ea::string> postProcessAntialiasingNames =
{
    "None",
    "FXAA2",
    "FXAA3"
};

static const ea::vector<ea::string> toneMappingModeNames =
{
    "None",
    "Reinhard",
    "ReinhardWhite",
    "Uncharted2",
};

}

RenderPipelineView::RenderPipelineView(RenderPipeline* renderPipeline)
    : Object(renderPipeline->GetContext())
    , renderPipeline_(renderPipeline)
    , graphics_(context_->GetSubsystem<Graphics>())
    , renderer_(context_->GetSubsystem<Renderer>())
{
    SetSettings(renderPipeline_->GetSettings());
    renderPipeline_->OnSettingsChanged.Subscribe(this, &RenderPipelineView::SetSettings);
}

RenderPipelineView::~RenderPipelineView()
{
}

const FrameInfo& RenderPipelineView::GetFrameInfo() const
{
    static const FrameInfo defaultFrameInfo;
    return sceneProcessor_ ? sceneProcessor_->GetFrameInfo() : defaultFrameInfo;
}

void RenderPipelineView::SetSettings(const RenderPipelineSettings& settings)
{
    settings_ = settings;
    settings_.Validate(context_);
    settingsDirty_ = true;
    settingsPipelineStateHash_ = settings_.CalculatePipelineStateHash();
}

void RenderPipelineView::SendViewEvent(StringHash eventType)
{
    using namespace BeginViewRender;

    VariantMap& eventData = GetEventDataMap();

    eventData[P_RENDERPIPELINEVIEW] = this;
    eventData[P_SURFACE] = frameInfo_.renderTarget_;
    eventData[P_TEXTURE] = frameInfo_.renderTarget_ ? frameInfo_.renderTarget_->GetParentTexture() : nullptr;
    eventData[P_SCENE] = sceneProcessor_->GetFrameInfo().scene_;
    eventData[P_CAMERA] = sceneProcessor_->GetFrameInfo().camera_;

    renderer_->SendEvent(eventType, eventData);
}

void RenderPipelineView::ApplySettings()
{
    sceneProcessor_->SetSettings(settings_.sceneProcessor_);
    instancingBuffer_->SetSettings(settings_.instancingBuffer_);
    shadowMapAllocator_->SetSettings(settings_.shadowMapAllocator_);

    if (!opaquePass_ || settings_.sceneProcessor_.IsDeferredLighting() != deferred_.has_value())
    {
        if (settings_.sceneProcessor_.IsDeferredLighting())
        {
            opaquePass_ = sceneProcessor_->CreatePass<UnorderedScenePass>(
                DrawableProcessorPassFlag::HasAmbientLighting | DrawableProcessorPassFlag::DeferredLightMaskToStencil,
                "deferred", "base", "litbase", "light");

            deferred_ = DeferredLightingData{};
            deferred_->albedoBuffer_ = renderBufferManager_->CreateColorBuffer({ Graphics::GetRGBAFormat() });
            deferred_->specularBuffer_ = renderBufferManager_->CreateColorBuffer({ Graphics::GetRGBAFormat() });
            deferred_->normalBuffer_ = renderBufferManager_->CreateColorBuffer({ Graphics::GetRGBAFormat() });
        }
        else
        {
            opaquePass_ = sceneProcessor_->CreatePass<UnorderedScenePass>(
                DrawableProcessorPassFlag::HasAmbientLighting,
                "", "base", "litbase", "light");

            deferred_ = ea::nullopt;
        }
    }

    sceneProcessor_->SetPasses({ opaquePass_, refractPass_, alphaPass_, postOpaquePass_ });

    postProcessPasses_.clear();

    if (settings_.renderBufferManager_.colorSpace_ == RenderPipelineColorSpace::LinearHDR)
    {
        auto pass = MakeShared<AutoExposurePass>(this, renderBufferManager_);
        pass->SetSettings(settings_.autoExposure_);
        postProcessPasses_.push_back(pass);
    }

    if (settings_.bloom_.enabled_)
    {
        auto pass = MakeShared<BloomPass>(this, renderBufferManager_);
        pass->SetSettings(settings_.bloom_);
        postProcessPasses_.push_back(pass);
    }

    if (settings_.renderBufferManager_.colorSpace_ == RenderPipelineColorSpace::LinearHDR)
    {
        auto pass = MakeShared<ToneMappingPass>(this, renderBufferManager_);
        pass->SetMode(settings_.toneMapping_);
        postProcessPasses_.push_back(pass);
    }

    switch (settings_.antialiasing_)
    {
    case PostProcessAntialiasing::FXAA2:
    {
        auto pass = MakeShared<SimplePostProcessPass>(this, renderBufferManager_,
            PostProcessPassFlag::NeedColorOutputReadAndWrite | PostProcessPassFlag::NeedColorOutputBilinear,
            BLEND_REPLACE, "v2/P_FXAA2", "");
        pass->AddShaderParameter("FXAAParams", Vector3(0.4f, 0.5f, 0.75f));
        postProcessPasses_.push_back(pass);
        break;
    }
    case PostProcessAntialiasing::FXAA3:
    {
        auto pass = MakeShared<SimplePostProcessPass>(this, renderBufferManager_,
            PostProcessPassFlag::NeedColorOutputReadAndWrite | PostProcessPassFlag::NeedColorOutputBilinear,
            BLEND_REPLACE, "v2/P_FXAA3", "FXAA_QUALITY_PRESET=12");
        postProcessPasses_.push_back(pass);
        break;
    }
    default:
        break;
    }

    if (settings_.greyScale_)
    {
        auto pass = MakeShared<SimplePostProcessPass>(this, renderBufferManager_,
            PostProcessPassFlag::NeedColorOutputReadAndWrite,
            BLEND_REPLACE, "v2/P_GreyScale", "");
        postProcessPasses_.push_back(pass);
    }

    postProcessFlags_ = {};
    for (PostProcessPass* postProcessPass : postProcessPasses_)
        postProcessFlags_ |= postProcessPass->GetExecutionFlags();

    const bool isDeferredLighting = settings_.sceneProcessor_.IsDeferredLighting();
    settings_.renderBufferManager_.filteredColor_ = postProcessFlags_.Test(PostProcessPassFlag::NeedColorOutputBilinear);
    settings_.renderBufferManager_.colorUsableWithMultipleRenderTargets_ = isDeferredLighting;
    settings_.renderBufferManager_.stencilBuffer_ = isDeferredLighting;
    settings_.renderBufferManager_.inheritMultiSampleLevel_ = !isDeferredLighting;
    renderBufferManager_->SetSettings(settings_.renderBufferManager_);
}

bool RenderPipelineView::Define(RenderSurface* renderTarget, Viewport* viewport)
{
    // Lazy initialize heavy objects
    if (!sceneProcessor_)
    {
        renderBufferManager_ = MakeShared<RenderBufferManager>(this);
        shadowMapAllocator_ = MakeShared<ShadowMapAllocator>(context_);
        instancingBuffer_ = MakeShared<InstancingBuffer>(context_);
        sceneProcessor_ = MakeShared<SceneProcessor>(this, "shadow", shadowMapAllocator_, instancingBuffer_);

        refractPass_ = sceneProcessor_->CreatePass<BackToFrontScenePass>(DrawableProcessorPassFlag::None, "refract");
        alphaPass_ = sceneProcessor_->CreatePass<BackToFrontScenePass>(
            DrawableProcessorPassFlag::HasAmbientLighting | DrawableProcessorPassFlag::SoftParticlesPass
            | DrawableProcessorPassFlag::RefractionPass, "", "alpha", "alpha", "litalpha");
        postOpaquePass_ = sceneProcessor_->CreatePass<UnorderedScenePass>(DrawableProcessorPassFlag::None, "postopaque");
    }

    frameInfo_.viewport_ = viewport;
    frameInfo_.renderTarget_ = renderTarget;
    frameInfo_.viewportRect_ = viewport->GetEffectiveRect(renderTarget);
    frameInfo_.viewportSize_ = frameInfo_.viewportRect_.Size();

    if (!sceneProcessor_->Define(frameInfo_))
        return false;

    sceneProcessor_->SetRenderCamera(viewport->GetCamera());

    if (settingsDirty_)
    {
        settingsDirty_ = false;
        ApplySettings();
    }

    return true;
}

void RenderPipelineView::Update(const FrameInfo& frameInfo)
{
    frameInfo_.frameNumber_ = frameInfo.frameNumber_;
    frameInfo_.timeStep_ = frameInfo.timeStep_;

    // Begin debug snapshot
#if URHO3D_SYSTEMUI
    const bool shiftDown = ui::IsKeyDown(KEY_LSHIFT) || ui::IsKeyDown(KEY_RSHIFT);
    const bool ctrlDown = ui::IsKeyDown(KEY_LCTRL) || ui::IsKeyDown(KEY_RCTRL);
    const bool takeSnapshot = shiftDown && ctrlDown && ui::IsKeyPressed(KEY_F12);
#else
    auto input = GetSubsystem<Input>();
    const bool takeSnapshot = input->GetQualifiers().Test(QUAL_CTRL | QUAL_SHIFT) && input->GetKeyPress(KEY_F12);
#endif
    if (takeSnapshot)
        debugger_.BeginSnapshot();

    // Begin update. Should happen before pipeline state hash check.
    shadowMapAllocator_->ResetAllShadowMaps();
    OnUpdateBegin(this, frameInfo_);
    SendViewEvent(E_BEGINVIEWUPDATE);

    // Invalidate pipeline states if necessary
    const unsigned pipelineStateHash = RecalculatePipelineStateHash();
    if (oldPipelineStateHash_ != pipelineStateHash)
    {
        oldPipelineStateHash_ = pipelineStateHash;
        OnPipelineStatesInvalidated(this);
    }

    sceneProcessor_->Update();

    SendViewEvent(E_ENDVIEWUPDATE);
    OnUpdateEnd(this, frameInfo_);
}

void RenderPipelineView::Render()
{
    const bool hasRefraction = refractPass_->HasBatches() || alphaPass_->HasRefractionBatches();
    RenderBufferManagerFrameSettings frameSettings;
    frameSettings.supportColorReadWrite_ = postProcessFlags_.Test(PostProcessPassFlag::NeedColorOutputReadAndWrite);
    frameSettings.readableDepth_ = settings_.sceneProcessor_.IsDeferredLighting() || settings_.sceneProcessor_.softParticles_;
    if (hasRefraction)
        frameSettings.supportColorReadWrite_ = true;
    renderBufferManager_->SetFrameSettings(frameSettings);

    OnRenderBegin(this, frameInfo_);
    SendViewEvent(E_BEGINVIEWRENDER);
    SendViewEvent(E_VIEWBUFFERSREADY);

    // TODO(renderer): Do something about this hack
    graphics_->SetVertexBuffer(nullptr);

    sceneProcessor_->RenderShadowMaps();

    Camera* camera = sceneProcessor_->GetFrameInfo().camera_;
    const Color fogColorInGammaSpace = sceneProcessor_->GetFrameInfo().camera_->GetEffectiveFogColor();
    const Color effectiveFogColor = settings_.sceneProcessor_.linearSpaceLighting_
        ? fogColorInGammaSpace.GammaToLinear()
        : fogColorInGammaSpace;
    auto drawQueue = renderer_->GetDefaultDrawQueue();

    // TODO(renderer): Remove this guard
#ifdef DESKTOP_GRAPHICS
    if (settings_.sceneProcessor_.IsDeferredLighting())
    {
        // Draw deferred GBuffer
        renderBufferManager_->ClearColor(deferred_->albedoBuffer_, Color::TRANSPARENT_BLACK);
        renderBufferManager_->ClearColor(deferred_->specularBuffer_, Color::TRANSPARENT_BLACK);
        renderBufferManager_->ClearOutput(effectiveFogColor, 1.0f, 0);

        RenderBuffer* const gBuffer[] = {
            renderBufferManager_->GetColorOutput(),
            deferred_->albedoBuffer_,
            deferred_->specularBuffer_,
            deferred_->normalBuffer_
        };
        renderBufferManager_->SetRenderTargets(renderBufferManager_->GetDepthStencilOutput(), gBuffer);
        sceneProcessor_->RenderSceneBatches("GeometryBuffer", camera,
            opaquePass_->GetDeferredRenderFlags(), opaquePass_->GetSortedDeferredBatches());

        // Draw deferred lights
        const ShaderResourceDesc geometryBuffer[] = {
            { TU_DIFFUSE, deferred_->albedoBuffer_->GetTexture() },
            { TU_SPECULAR, deferred_->specularBuffer_->GetTexture() },
            { TU_NORMAL, deferred_->normalBuffer_->GetTexture() },
            { TU_DEPTHBUFFER, renderBufferManager_->GetDepthStencilTexture() }
        };

        const ShaderParameterDesc cameraParameters[] = {
            { VSP_GBUFFEROFFSETS, renderBufferManager_->GetDefaultClipToUVSpaceOffsetAndScale() },
            { PSP_GBUFFERINVSIZE, renderBufferManager_->GetInvOutputSize() },
        };

        BatchRenderingContext ctx{ *drawQueue, *sceneProcessor_->GetFrameInfo().camera_ };
        ctx.globalResources_ = geometryBuffer;
        ctx.cameraParameters_ = cameraParameters;

        renderBufferManager_->SetOutputRenderTargers();
        sceneProcessor_->RenderLightVolumeBatches("LightVolumes", camera, geometryBuffer, cameraParameters);
    }
    else
#endif
    {
        renderBufferManager_->ClearOutput(effectiveFogColor, 1.0f, 0);
        renderBufferManager_->SetOutputRenderTargers();
    }

    sceneProcessor_->RenderSceneBatches("OpaqueBase", camera,
        opaquePass_->GetBaseRenderFlags(), opaquePass_->GetSortedBaseBatches());
    sceneProcessor_->RenderSceneBatches("OpaqueLight", camera,
        opaquePass_->GetLightRenderFlags(), opaquePass_->GetSortedLightBatches());
    sceneProcessor_->RenderSceneBatches("PostOpaque", camera,
        postOpaquePass_->GetBaseRenderFlags(), postOpaquePass_->GetSortedBaseBatches());

    if (hasRefraction)
        renderBufferManager_->PrepareForColorReadWrite(true);

#ifdef DESKTOP_GRAPHICS
    ShaderResourceDesc depthAndColorTextures[] = {
        { TU_DEPTHBUFFER, renderBufferManager_->GetDepthStencilTexture() },
        { TU_EMISSIVE, renderBufferManager_->GetSecondaryColorTexture() },
    };
#else
    ShaderResourceDesc depthAndColorTextures[] = {
        { TU_EMISSIVE, renderBufferManager_->GetSecondaryColorTexture() },
    };
#endif

    drawQueue->Reset();
    instancingBuffer_->Begin();

    const ShaderParameterDesc cameraParameters[] = {
        { VSP_GBUFFEROFFSETS, renderBufferManager_->GetDefaultClipToUVSpaceOffsetAndScale() },
        { PSP_GBUFFERINVSIZE, renderBufferManager_->GetInvOutputSize() },
    };
    sceneProcessor_->RenderSceneBatches("Alpha", camera,
        alphaPass_->GetRenderFlags(), alphaPass_->GetSortedBatches(),
        depthAndColorTextures, cameraParameters);

    for (PostProcessPass* postProcessPass : postProcessPasses_)
        postProcessPass->Execute();

    auto debug = sceneProcessor_->GetFrameInfo().scene_->GetComponent<DebugRenderer>();
    if (debug && debug->IsEnabledEffective() && debug->HasContent())
    {
        renderBufferManager_->SetOutputRenderTargers();
        debug->SetView(sceneProcessor_->GetFrameInfo().camera_);
        debug->Render();
    }

    SendViewEvent(E_ENDVIEWRENDER);
    OnRenderEnd(this, frameInfo_);
    graphics_->SetColorWrite(true);

    // End debug snapshot
    if (debugger_.IsSnapshotInProgress())
    {
        debugger_.EndSnapshot();

        const DebugFrameSnapshot& snapshot = debugger_.GetSnapshot();
        URHO3D_LOGINFO("RenderPipeline snapshot:\n\n{}\n", snapshot.ToString());
    }
}

unsigned RenderPipelineView::RecalculatePipelineStateHash() const
{
    unsigned hash = settingsPipelineStateHash_;
    CombineHash(hash, sceneProcessor_->GetCameraProcessor()->GetPipelineStateHash());
    return hash;
}

RenderPipeline::RenderPipeline(Context* context)
    : Component(context)
{
    // Enable instancing by default for default render pipeline
    settings_.instancingBuffer_.enableInstancing_ = true;
    settings_.Validate(context_);
}

RenderPipeline::~RenderPipeline()
{
}

void RenderPipeline::RegisterObject(Context* context)
{
    context->RegisterFactory<RenderPipeline>();

    URHO3D_ENUM_ATTRIBUTE_EX("Color Space", settings_.renderBufferManager_.colorSpace_, MarkSettingsDirty, colorSpaceNames, RenderPipelineColorSpace::GammaLDR, AM_DEFAULT);
    URHO3D_ENUM_ATTRIBUTE_EX("Material Quality", settings_.sceneProcessor_.materialQuality_, MarkSettingsDirty, materialQualityNames, SceneProcessorSettings{}.materialQuality_, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Max Vertex Lights", int, settings_.sceneProcessor_.maxVertexLights_, MarkSettingsDirty, 4, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Max Pixel Lights", int, settings_.sceneProcessor_.maxPixelLights_, MarkSettingsDirty, 4, AM_DEFAULT);
    URHO3D_ENUM_ATTRIBUTE_EX("Ambient Mode", settings_.sceneProcessor_.ambientMode_, MarkSettingsDirty, ambientModeNames, DrawableAmbientMode::Directional, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Enable Instancing", bool, settings_.instancingBuffer_.enableInstancing_, MarkSettingsDirty, true, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Enable Shadows", bool, settings_.sceneProcessor_.enableShadows_, MarkSettingsDirty, true, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Soft Particles", bool, settings_.sceneProcessor_.softParticles_, MarkSettingsDirty, false, AM_DEFAULT);
    URHO3D_ENUM_ATTRIBUTE_EX("Lighting Mode", settings_.sceneProcessor_.lightingMode_, MarkSettingsDirty, directLightingModeNames, DirectLightingMode::Forward, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Specular Anti-Aliasing", bool, settings_.sceneProcessor_.specularAntiAliasing_, MarkSettingsDirty, false, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("PCF Kernel Size", unsigned, settings_.sceneProcessor_.pcfKernelSize_, MarkSettingsDirty, 1, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Use Variance Shadow Maps", bool, settings_.shadowMapAllocator_.enableVarianceShadowMaps_, MarkSettingsDirty, false, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("VSM Shadow Settings", Vector2, settings_.sceneProcessor_.varianceShadowMapParams_, MarkSettingsDirty, BatchRendererSettings{}.varianceShadowMapParams_, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("VSM Multi Sample", int, settings_.shadowMapAllocator_.varianceShadowMapMultiSample_, MarkSettingsDirty, 1, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("16-bit Shadow Maps", bool, settings_.shadowMapAllocator_.use16bitShadowMaps_, MarkSettingsDirty, false, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Auto Exposure", bool, settings_.autoExposure_.autoExposure_, MarkSettingsDirty, false, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Min Exposure", float, settings_.autoExposure_.minExposure_, MarkSettingsDirty, AutoExposurePassSettings{}.minExposure_, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Max Exposure", float, settings_.autoExposure_.maxExposure_, MarkSettingsDirty, AutoExposurePassSettings{}.maxExposure_, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Adapt Rate", float, settings_.autoExposure_.adaptRate_, MarkSettingsDirty, AutoExposurePassSettings{}.adaptRate_, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Bloom", bool, settings_.bloom_.enabled_, MarkSettingsDirty, BloomPassSettings{}.enabled_, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Bloom Threshold", float, settings_.bloom_.threshold_, MarkSettingsDirty, BloomPassSettings{}.threshold_, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Bloom Intensity", float, settings_.bloom_.bloomIntensity_, MarkSettingsDirty, BloomPassSettings{}.bloomIntensity_, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Bloom Source Multiplier", float, settings_.bloom_.sourceIntensity_, MarkSettingsDirty, BloomPassSettings{}.sourceIntensity_, AM_DEFAULT);
    URHO3D_ENUM_ATTRIBUTE_EX("Tone Mapping Mode", settings_.toneMapping_, MarkSettingsDirty, toneMappingModeNames, ToneMappingMode::None, AM_DEFAULT);
    URHO3D_ENUM_ATTRIBUTE_EX("Post Process Antialiasing", settings_.antialiasing_, MarkSettingsDirty, postProcessAntialiasingNames, PostProcessAntialiasing::None, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Post Process Grey Scale", bool, settings_.greyScale_, MarkSettingsDirty, false, AM_DEFAULT);
}

void RenderPipeline::SetSettings(const RenderPipelineSettings& settings)
{
    settings_ = settings;
    settings_.Validate(context_);
    MarkSettingsDirty();
}

SharedPtr<RenderPipelineView> RenderPipeline::Instantiate()
{
    return MakeShared<RenderPipelineView>(this);
}

void RenderPipeline::MarkSettingsDirty()
{
    settings_.Validate(context_);
    OnSettingsChanged(this, settings_);
}

}
