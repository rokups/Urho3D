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
#include "../RenderPipeline/RenderPipeline.h"
#include "../RenderPipeline/DefaultRenderPipeline.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Renderer.h"
/*
#include "../Graphics/GraphicsEvents.h"
#include "../Graphics/Texture2D.h"
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
#include "../RenderPipeline/ShaderConsts.h"
#include "../Scene/Scene.h"
#if URHO3D_SYSTEMUI
    #include "../SystemUI/SystemUI.h"
#endif

#include <EASTL/fixed_vector.h>
#include <EASTL/vector_map.h>
#include <EASTL/sort.h>*/

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

static const ea::vector<ea::string> specularQualityNames =
{
    "Disabled",
    "Simple",
    "Antialiased",
};

static const ea::vector<ea::string> reflectionQualityNames =
{
    "Vertex",
    "Pixel",
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
}

RenderPipeline::RenderPipeline(Context* context)
    : Component(context)
{
    // Enable instancing by default for default render pipeline
    settings_.instancingBuffer_.enableInstancing_ = true;
    settings_.Validate();
    settings_.AdjustToSupported(context_);
}

RenderPipeline::~RenderPipeline()
{
}

void RenderPipeline::RegisterObject(Context* context)
{
    context->RegisterFactory<RenderPipeline>();
    URHO3D_ENUM_ATTRIBUTE_EX("Color Space", settings_.renderBufferManager_.colorSpace_, MarkSettingsDirty, colorSpaceNames, RenderPipelineColorSpace::GammaLDR, AM_DEFAULT);
    URHO3D_ENUM_ATTRIBUTE_EX("Material Quality", settings_.sceneProcessor_.materialQuality_, MarkSettingsDirty, materialQualityNames, SceneProcessorSettings{}.materialQuality_, AM_DEFAULT);
    URHO3D_ENUM_ATTRIBUTE_EX("Specular Quality", settings_.sceneProcessor_.specularQuality_, MarkSettingsDirty, specularQualityNames, SceneProcessorSettings{}.specularQuality_, AM_DEFAULT);
    URHO3D_ENUM_ATTRIBUTE_EX("Reflection Quality", settings_.sceneProcessor_.reflectionQuality_, MarkSettingsDirty, reflectionQualityNames, SceneProcessorSettings{}.reflectionQuality_, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Readable Depth", bool, settings_.renderBufferManager_.readableDepth_, MarkSettingsDirty, RenderBufferManagerSettings{}.readableDepth_, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Max Vertex Lights", unsigned, settings_.sceneProcessor_.maxVertexLights_, MarkSettingsDirty, DrawableProcessorSettings{}.maxVertexLights_, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Max Pixel Lights", unsigned, settings_.sceneProcessor_.maxPixelLights_, MarkSettingsDirty, DrawableProcessorSettings{}.maxPixelLights_, AM_DEFAULT);
    URHO3D_ENUM_ATTRIBUTE_EX("Ambient Mode", settings_.sceneProcessor_.ambientMode_, MarkSettingsDirty, ambientModeNames, DrawableAmbientMode::Directional, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Enable Instancing", bool, settings_.instancingBuffer_.enableInstancing_, MarkSettingsDirty, true, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Depth Pre-Pass", bool, settings_.sceneProcessor_.depthPrePass_, MarkSettingsDirty, false, AM_DEFAULT);
    URHO3D_ENUM_ATTRIBUTE_EX("Lighting Mode", settings_.sceneProcessor_.lightingMode_, MarkSettingsDirty, directLightingModeNames, DirectLightingMode::Forward, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Enable Shadows", bool, settings_.sceneProcessor_.enableShadows_, MarkSettingsDirty, true, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("PCF Kernel Size", unsigned, settings_.sceneProcessor_.pcfKernelSize_, MarkSettingsDirty, 1, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Use Variance Shadow Maps", bool, settings_.shadowMapAllocator_.enableVarianceShadowMaps_, MarkSettingsDirty, false, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("VSM Shadow Settings", Vector2, settings_.sceneProcessor_.varianceShadowMapParams_, MarkSettingsDirty, BatchRendererSettings{}.varianceShadowMapParams_, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("VSM Multi Sample", unsigned, settings_.shadowMapAllocator_.varianceShadowMapMultiSample_, MarkSettingsDirty, 1, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("16-bit Shadow Maps", bool, settings_.shadowMapAllocator_.use16bitShadowMaps_, MarkSettingsDirty, false, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Auto Exposure", bool, settings_.autoExposure_.autoExposure_, MarkSettingsDirty, false, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Min Exposure", float, settings_.autoExposure_.minExposure_, MarkSettingsDirty, AutoExposurePassSettings{}.minExposure_, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Max Exposure", float, settings_.autoExposure_.maxExposure_, MarkSettingsDirty, AutoExposurePassSettings{}.maxExposure_, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Adapt Rate", float, settings_.autoExposure_.adaptRate_, MarkSettingsDirty, AutoExposurePassSettings{}.adaptRate_, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Bloom", bool, settings_.bloom_.enabled_, MarkSettingsDirty, BloomPassSettings{}.enabled_, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Bloom Iterations", unsigned, settings_.bloom_.numIterations_, MarkSettingsDirty, BloomPassSettings{}.numIterations_, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Bloom Threshold", float, settings_.bloom_.threshold_, MarkSettingsDirty, BloomPassSettings{}.threshold_, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Bloom Threshold Max", float, settings_.bloom_.thresholdMax_, MarkSettingsDirty, BloomPassSettings{}.thresholdMax_, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Bloom Intensity", float, settings_.bloom_.intensity_, MarkSettingsDirty, BloomPassSettings{}.intensity_, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Bloom Iteration Factor", float, settings_.bloom_.iterationFactor_, MarkSettingsDirty, BloomPassSettings{}.iterationFactor_, AM_DEFAULT);
    URHO3D_ENUM_ATTRIBUTE_EX("Tone Mapping Mode", settings_.toneMapping_, MarkSettingsDirty, toneMappingModeNames, ToneMappingMode::None, AM_DEFAULT);
    URHO3D_ENUM_ATTRIBUTE_EX("Post Process Antialiasing", settings_.antialiasing_, MarkSettingsDirty, postProcessAntialiasingNames, PostProcessAntialiasing::None, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Post Process Grey Scale", bool, settings_.greyScale_, MarkSettingsDirty, false, AM_DEFAULT);
}

void RenderPipeline::SetSettings(const RenderPipelineSettings& settings)
{
    settings_ = settings;
    settings_.Validate();
    settings_.AdjustToSupported(context_);
    MarkSettingsDirty();
}

SharedPtr<RenderPipelineView> RenderPipeline::Instantiate()
{
    return MakeShared<DefaultRenderPipelineView>(this);
}

void RenderPipeline::MarkSettingsDirty()
{
    settings_.Validate();
    settings_.AdjustToSupported(context_);
    OnSettingsChanged(this, settings_);
}

}
