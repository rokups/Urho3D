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
#include "../Graphics/ShaderProgramLayout.h"
#include "../Graphics/DebugRenderer.h"
#include "../Graphics/DrawCommandQueue.h"
#include "../Graphics/IndexBuffer.h"
#include "../Graphics/OcclusionBuffer.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/Viewport.h"
#include "../RenderPipeline/RenderPipeline.h"
#include "../RenderPipeline/DrawableProcessor.h"
#include "../RenderPipeline/BatchRenderer.h"
#include "../RenderPipeline/ShadowMapAllocator.h"
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

int GetTextureColorSpaceHint(bool linearInput, bool srgbTexture)
{
    return static_cast<int>(linearInput) + static_cast<int>(srgbTexture);
}

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

static const ea::vector<ea::string> ambientModeNames =
{
    "Constant",
    "Flat",
    "Directional",
};

static const ea::vector<ea::string> postProcessAntialiasingNames =
{
    "None",
    "FXAA2",
    "FXAA3"
};

static const ea::vector<ea::string> postProcessTonemappingNames =
{
    "None",
    "ReinhardEq3",
    "ReinhardEq4",
    "Uncharted2",
};

/*enum class ShaderInputFlag
{
    None = 0,
    VertexHasNormal     = 1 << 0,
    VertexHasTangent    = 1 << 1,
    VertexHasColor      = 1 << 2,
    VertexHasTexCoord0  = 1 << 3,
    MaterialHasDiffuse  = 1 << 4,
    MaterialHasNormal   = 1 << 5,
    MaterialHasSpecular = 1 << 6,
    MaterialHasEmissive = 1 << 7,
};

URHO3D_FLAGSET(ShaderInputFlag, ShaderInputFlags);*/

}

RenderPipeline::RenderPipeline(Context* context)
    : RenderPipelineInterface(context)
    , graphics_(context_->GetSubsystem<Graphics>())
    , renderer_(context_->GetSubsystem<Renderer>())
    , workQueue_(context_->GetSubsystem<WorkQueue>())
{
}

RenderPipeline::~RenderPipeline()
{
}

void RenderPipeline::RegisterObject(Context* context)
{
    context->RegisterFactory<RenderPipeline>();

    URHO3D_ENUM_ATTRIBUTE_EX("Ambient Mode", settings_.ambientMode_, MarkSettingsDirty, ambientModeNames, DrawableAmbientMode::Directional, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Enable Instancing", bool, settings_.enableInstancing_, MarkSettingsDirty, false, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Enable Shadow", bool, settings_.enableShadows_, MarkSettingsDirty, true, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Deferred Lighting", bool, settings_.deferredLighting_, MarkSettingsDirty, false, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Use Variance Shadow Maps", bool, settings_.enableVarianceShadowMaps_, MarkSettingsDirty, false, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("VSM Shadow Settings", Vector2, settings_.varianceShadowMapParams_, MarkSettingsDirty, BatchRendererSettings{}.varianceShadowMapParams_, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("VSM Multi Sample", int, settings_.varianceShadowMapMultiSample_, MarkSettingsDirty, 1, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Gamma Correction", bool, settings_.gammaCorrection_, MarkSettingsDirty, false, AM_DEFAULT);
    URHO3D_ENUM_ATTRIBUTE_EX("Post Process Antialiasing", settings_.postProcess_.antialiasing_, MarkSettingsDirty, postProcessAntialiasingNames, PostProcessAntialiasing::None, AM_DEFAULT);
    URHO3D_ENUM_ATTRIBUTE_EX("Post Process Tonemapping", settings_.postProcess_.tonemapping_, MarkSettingsDirty, postProcessTonemappingNames, PostProcessTonemapping::None, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Post Process Grey Scale", bool, settings_.postProcess_.greyScale_, MarkSettingsDirty, false, AM_DEFAULT);
}

void RenderPipeline::ApplyAttributes()
{
}

SharedPtr<PipelineState> RenderPipeline::CreateBatchPipelineState(
    const BatchStateCreateKey& key, const BatchStateCreateContext& ctx)
{
    const bool isInternalPass = sceneProcessor_->IsInternalPass(ctx.pass_);
    if (isInternalPass && ctx.subpassIndex_ == BatchCompositor::LitVolumeSubpass)
    {
        return CreateLightVolumePipelineState(key.pixelLight_, key.geometry_);
    }

    Geometry* geometry = key.geometry_;
    Material* material = key.material_;
    Pass* pass = key.pass_;
    Light* light = key.pixelLight_ ? key.pixelLight_->GetLight() : nullptr;
    const bool isLitBasePass = ctx.subpassIndex_ == 1;
    const bool isShadowPass = isInternalPass;
    const bool isAdditiveLightPass = !isInternalPass && ctx.subpassIndex_ == 2;
    const bool isVertexLitPass = ctx.pass_ == alphaPass_ || ctx.pass_ == basePass_;
    const bool isAmbientLitPass = !isAdditiveLightPass && (isVertexLitPass || ctx.pass_ == deferredPass_);

    PipelineStateDesc desc;
    ea::string commonDefines;
    ea::string vertexShaderDefines;
    ea::string pixelShaderDefines;

    // Update vertex elements
    // TODO(renderer): Consider instancing buffer here
    desc.InitializeInputLayoutAndPrimitiveType(geometry);
    if (desc.numVertexElements_ == 0)
        return nullptr;

    // Update shadow parameters
    // TODO(renderer): Don't use ShadowMapAllocator for this
    if (isShadowPass)
    {
        const CookedLightParams& lightParams = key.pixelLight_->GetParams();
        const float biasMultiplier = lightParams.shadowDepthBiasMultiplier_[ctx.shadowSplitIndex_];
        const BiasParameters& biasParameters = light->GetShadowBias();
        desc.fillMode_ = FILL_SOLID;
        desc.stencilTestEnabled_ = false;
        if (!settings_.enableVarianceShadowMaps_)
        {
            desc.colorWriteEnabled_ = false;
            desc.constantDepthBias_ = biasMultiplier * biasParameters.constantBias_;
            desc.slopeScaledDepthBias_ = biasMultiplier * biasParameters.slopeScaledBias_;
        }
        else
        {
            desc.colorWriteEnabled_ = true;
            desc.constantDepthBias_ = 0.0f;
            desc.slopeScaledDepthBias_ = 0.0f;
        }

        // TODO(renderer): Revisit this place
        // Perform further modification of depth bias on OpenGL ES, as shadow calculations' precision is limited
#ifdef GL_ES_VERSION_2_0
        const float multiplier = renderer_->GetMobileShadowBiasMul();
        const float addition = renderer_->GetMobileShadowBiasAdd();
        desc.constantDepthBias_ = desc.constantDepthBias_ * multiplier + addition;
        desc.slopeScaledDepthBias_ *= multiplier;
#endif
    }

    if (isVertexLitPass)
        commonDefines += "URHO3D_NUM_VERTEX_LIGHTS=4 ";

    if (isAmbientLitPass)
        commonDefines += "URHO3D_AMBIENT_PASS ";

    if (isAdditiveLightPass)
        commonDefines += "URHO3D_ADDITIVE_PASS ";

    // Add lightmap
    const bool isLightmap = !!key.drawable_->GetBatches()[key.sourceBatchIndex_].lightmapScaleOffset_;
    if (isLightmap)
    {
        commonDefines += "URHO3D_HAS_LIGHTMAP ";
        commonDefines += "LIGHTMAP ";
    }

    // Add vertex defines: ambient
    static const ea::string ambientModeDefines[] = {
        "URHO3D_AMBIENT_CONSTANT ",
        "URHO3D_AMBIENT_FLAT ",
        "URHO3D_AMBIENT_DIRECTIONAL ",
    };
    vertexShaderDefines += ambientModeDefines[static_cast<int>(settings_.ambientMode_)];

    // Add vertex defines: geometry
    static const ea::string geometryTypeDefines[] = {
        "URHO3D_GEOMETRY_STATIC ",
        "URHO3D_GEOMETRY_SKINNED ",
        "URHO3D_GEOMETRY_STATIC ",
        "URHO3D_GEOMETRY_BILLBOARD ",
        "URHO3D_GEOMETRY_DIRBILLBOARD ",
        "URHO3D_GEOMETRY_TRAIL_FACE_CAMERA ",
        "URHO3D_GEOMETRY_TRAIL_BONE ",
        "URHO3D_GEOMETRY_STATIC ",
    };
    vertexShaderDefines += geometryTypeDefines[static_cast<int>(key.geometryType_)];

    // Add vertex input layout defines
    for (const VertexElement& element : desc.vertexElements_)
    {
        if (element.index_ != 0)
            continue;

        switch (element.semantic_)
        {
        case SEM_NORMAL:
            commonDefines += "URHO3D_VERTEX_HAS_NORMAL ";
            break;

        case SEM_TANGENT:
            commonDefines += "URHO3D_VERTEX_HAS_TANGENT ";
            break;

        case SEM_COLOR:
            commonDefines += "URHO3D_VERTEX_HAS_COLOR ";
            break;

        case SEM_TEXCOORD:
            vertexShaderDefines += "URHO3D_VERTEX_HAS_TEXCOORD0 ";
            break;

        default:
            break;
        }
    }

    // Add material defines
    if (Texture* diffuseTexture = material->GetTexture(TU_DIFFUSE))
    {
        pixelShaderDefines += "URHO3D_MATERIAL_HAS_DIFFUSE ";
        const bool isLinear = diffuseTexture->GetLinear();
        const bool sRGB = diffuseTexture->GetSRGB();
        const int hint = GetTextureColorSpaceHint(isLinear, sRGB);
        // TODO(renderer): Throttle logging
        if (hint > 1)
            URHO3D_LOGWARNING("Texture {} cannot be both sRGB and Linear", diffuseTexture->GetName());
        pixelShaderDefines += Format("URHO3D_MATERIAL_DIFFUSE_HINT={} ", ea::min(1, hint));
    }
    if (material->GetTexture(TU_NORMAL))
        pixelShaderDefines += "URHO3D_MATERIAL_HAS_NORMAL ";
    if (material->GetTexture(TU_SPECULAR))
        pixelShaderDefines += "URHO3D_MATERIAL_HAS_SPECULAR ";
    if (material->GetTexture(TU_EMISSIVE))
        pixelShaderDefines += "URHO3D_MATERIAL_HAS_EMISSIVE ";

    // Add geometry type defines
    if (key.geometryType_ == GEOM_STATIC && settings_.enableInstancing_)
        vertexShaderDefines += "URHO3D_INSTANCING ";

    if (isShadowPass)
    {
        commonDefines += "PASS_SHADOW ";
        // TODO(renderer): Add define for normal offset
        if (light->GetShadowBias().normalOffset_ > 0.0)
            vertexShaderDefines += "NORMALOFFSET ";
    }
    else if (ctx.pass_ == deferredPass_)
        commonDefines += "PASS_DEFERRED ";
    else if (ctx.pass_ == basePass_)
    {
        commonDefines += "PASS_BASE ";
        switch (ctx.subpassIndex_)
        {
        case 0: commonDefines += "PASS_BASE_UNLIT "; break;
        case 1: commonDefines += "PASS_BASE_LITBASE "; break;
        case 2: commonDefines += "PASS_BASE_LIGHT "; break;
        }
    }
    else if (ctx.pass_ == alphaPass_)
    {
        commonDefines += "PASS_ALPHA ";
        switch (ctx.subpassIndex_)
        {
        case 0: commonDefines += "PASS_ALPHA_UNLIT "; break;
        case 1: commonDefines += "PASS_ALPHA_LITBASE "; break;
        case 2: commonDefines += "PASS_ALPHA_LIGHT "; break;
        }
    }

    // TODO(renderer): This check is not correct, have to check for AMBIENT is pass defines
    if (isLitBasePass || ctx.subpassIndex_ == 0)
        commonDefines += "NUMVERTEXLIGHTS=4 AMBIENT ";

    if (light)
    {
        commonDefines += "PERPIXEL ";
        if (key.pixelLight_->HasShadow())
        {
            if (settings_.enableVarianceShadowMaps_)
                commonDefines += "SHADOW VSM_SHADOW ";
            else
                commonDefines += "SHADOW SIMPLE_SHADOW ";
        }
        switch (light->GetLightType())
        {
        case LIGHT_DIRECTIONAL:
            commonDefines += "DIRLIGHT ";
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

    if (settings_.gammaCorrection_)
        commonDefines += "URHO3D_GAMMA_CORRECTION ";

    vertexShaderDefines += commonDefines;
    vertexShaderDefines += pass->GetEffectiveVertexShaderDefines();
    pixelShaderDefines += commonDefines;
    pixelShaderDefines += pass->GetEffectivePixelShaderDefines();
    desc.vertexShader_ = graphics_->GetShader(VS, "v2/" + pass->GetVertexShader(), vertexShaderDefines);
    desc.pixelShader_ = graphics_->GetShader(PS, "v2/" + pass->GetPixelShader(), pixelShaderDefines);

    desc.depthWriteEnabled_ = pass->GetDepthWrite();
    desc.depthCompareFunction_ = pass->GetDepthTestMode();
    desc.stencilTestEnabled_ = false;
    desc.stencilCompareFunction_ = CMP_ALWAYS;

    desc.colorWriteEnabled_ = true;
    desc.blendMode_ = pass->GetBlendMode();
    desc.alphaToCoverageEnabled_ = pass->GetAlphaToCoverage();

    desc.fillMode_ = FILL_SOLID;
    const CullMode passCullMode = pass->GetCullMode();
    const CullMode materialCullMode = isShadowPass ? material->GetShadowCullMode() : material->GetCullMode();
    const CullMode cullMode = passCullMode != MAX_CULLMODES ? passCullMode : materialCullMode;
    desc.cullMode_ = isShadowPass ? cullMode : GetEffectiveCullMode(cullMode, sceneProcessor_->GetFrameInfo().camera_);

    if (ctx.pass_ == deferredPass_)
    {
        desc.stencilTestEnabled_ = true;
        desc.stencilOperationOnPassed_ = OP_REF;
        desc.stencilWriteMask_ = PORTABLE_LIGHTMASK;
        desc.stencilReferenceValue_ = key.drawable_->GetLightMaskInZone() & PORTABLE_LIGHTMASK;
    }

    return renderer_->GetOrCreatePipelineState(desc);
}

SharedPtr<PipelineState> RenderPipeline::CreateLightVolumePipelineState(LightProcessor* sceneLight, Geometry* lightGeometry)
{
    ea::string vertexDefines;
    ea::string pixelDefiles = "HWDEPTH ";

    if (graphics_->GetConstantBuffersEnabled())
    {
        vertexDefines += "URHO3D_USE_CBUFFERS ";
        pixelDefiles += "URHO3D_USE_CBUFFERS ";
    }

    if (settings_.gammaCorrection_)
    {
        vertexDefines += "URHO3D_GAMMA_CORRECTION ";
        pixelDefiles += "URHO3D_GAMMA_CORRECTION ";
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
        if (settings_.enableVarianceShadowMaps_)
            pixelDefiles += "SHADOW VSM_SHADOW ";
        else
            pixelDefiles += "SHADOW SIMPLE_SHADOW ";
        // TODO(renderer): Add define for normal offset
        //if (light->GetShadowBias().normalOffset_ > 0.0)
        //    pixelDefiles += "NORMALOFFSET ";
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
    desc.InitializeInputLayoutAndPrimitiveType(lightGeometry);
    desc.stencilTestEnabled_ = false;
    desc.stencilCompareFunction_ = CMP_ALWAYS;

    desc.colorWriteEnabled_ = true;
    desc.blendMode_ = light->IsNegative() ? BLEND_SUBTRACT : BLEND_ADD;
    desc.alphaToCoverageEnabled_ = false;

    desc.fillMode_ = FILL_SOLID;

    if (type != LIGHT_DIRECTIONAL)
    {
        if (sceneLight->DoesOverlapCamera())
        {
            desc.cullMode_ = GetEffectiveCullMode(CULL_CW, sceneProcessor_->GetFrameInfo().camera_);
            desc.depthCompareFunction_ = CMP_GREATER;
        }
        else
        {
            desc.cullMode_ = GetEffectiveCullMode(CULL_CCW, sceneProcessor_->GetFrameInfo().camera_);
            desc.depthCompareFunction_ = CMP_LESSEQUAL;
        }
    }
    else
    {
        desc.cullMode_ = CULL_NONE;
        desc.depthCompareFunction_ = CMP_ALWAYS;
    }

    desc.vertexShader_ = graphics_->GetShader(VS, "v2/DeferredLight", vertexDefines);
    desc.pixelShader_ = graphics_->GetShader(PS, "v2/DeferredLight", pixelDefiles);

    desc.stencilTestEnabled_ = true;
    desc.stencilCompareFunction_ = CMP_NOTEQUAL;
    desc.stencilCompareMask_ = light->GetLightMaskEffective() & PORTABLE_LIGHTMASK;
    desc.stencilReferenceValue_ = 0;

    return renderer_->GetOrCreatePipelineState(desc);
}

void RenderPipeline::ValidateSettings()
{
    if (settings_.deferredLighting_ && !graphics_->GetDeferredSupport()
        && !Graphics::GetReadableDepthStencilFormat())
        settings_.deferredLighting_ = false;

    if (!settings_.use16bitShadowMaps_ && !graphics_->GetHiresShadowMapFormat())
        settings_.use16bitShadowMaps_ = true;

    if (settings_.enableInstancing_ && !graphics_->GetInstancingSupport())
        settings_.enableInstancing_ = false;

    if (settings_.enableInstancing_)
    {
        settings_.firstInstancingTexCoord_ = 4;
        settings_.numInstancingTexCoords_ = 3 +
            (settings_.ambientMode_ == DrawableAmbientMode::Constant
            ? 0
            : settings_.ambientMode_ == DrawableAmbientMode::Flat
            ? 1
            : 7);
    }
}

void RenderPipeline::ApplySettings()
{
    sceneProcessor_->SetSettings(settings_);
    instancingBuffer_->SetSettings(settings_);
    shadowMapAllocator_->SetSettings(settings_);

    // Initialize or reset deferred rendering
    // TODO(renderer): Make it more clean
    if (settings_.deferredLighting_ && !deferredPass_)
    {
        basePass_ = nullptr;
        alphaPass_ = nullptr;
        deferredPass_ = MakeShared<UnlitScenePass>(this, sceneProcessor_->GetDrawableProcessor(), "deferred");

        deferredAlbedo_ = renderBufferManager_->CreateColorBuffer({ Graphics::GetRGBAFormat() });
        deferredNormal_ = renderBufferManager_->CreateColorBuffer({ Graphics::GetRGBAFormat() });

        sceneProcessor_->SetPasses({ deferredPass_ });
    }

    if (!settings_.deferredLighting_ && !basePass_)
    {
        basePass_ = MakeShared<OpaqueForwardLightingScenePass>(this, sceneProcessor_->GetDrawableProcessor(), "base", "litbase", "light");
        alphaPass_ = MakeShared<AlphaForwardLightingScenePass>(this, sceneProcessor_->GetDrawableProcessor(), "alpha", "alpha", "litalpha");
        deferredPass_ = nullptr;

        deferredAlbedo_ = nullptr;
        deferredNormal_ = nullptr;

        sceneProcessor_->SetPasses({ basePass_, alphaPass_ });
    }

    postProcessPasses_.clear();

    switch (settings_.postProcess_.antialiasing_)
    {
    case PostProcessAntialiasing::FXAA2:
    {
        auto pass = MakeShared<SimplePostProcessPass>(this, renderBufferManager_,
            PostProcessPassFlag::NeedColorOutputReadAndWrite | PostProcessPassFlag::NeedColorOutputBilinear,
            BLEND_REPLACE, "v2/PP_FXAA2", "");
        pass->AddShaderParameter("FXAAParams", Vector3(0.4f, 0.5f, 0.75f));
        postProcessPasses_.push_back(pass);
        break;
    }
    case PostProcessAntialiasing::FXAA3:
    {
        auto pass = MakeShared<SimplePostProcessPass>(this, renderBufferManager_,
            PostProcessPassFlag::NeedColorOutputReadAndWrite | PostProcessPassFlag::NeedColorOutputBilinear,
            BLEND_REPLACE, "v2/PP_FXAA3", "FXAA_QUALITY_PRESET=12");
        postProcessPasses_.push_back(pass);
        break;
    }
    default:
        break;
    }

    switch (settings_.postProcess_.tonemapping_)
    {
    case PostProcessTonemapping::ReinhardEq3:
    {
        auto pass = MakeShared<SimplePostProcessPass>(this, renderBufferManager_,
            PostProcessPassFlag::NeedColorOutputReadAndWrite,
            BLEND_REPLACE, "v2/PP_Tonemap", "REINHARDEQ3");
        pass->AddShaderParameter("TonemapExposureBias", 1.0f);
        postProcessPasses_.push_back(pass);
        break;
    }
    case PostProcessTonemapping::ReinhardEq4:
    {
        auto pass = MakeShared<SimplePostProcessPass>(this, renderBufferManager_,
            PostProcessPassFlag::NeedColorOutputReadAndWrite,
            BLEND_REPLACE, "v2/PP_Tonemap", "REINHARDEQ4");
        pass->AddShaderParameter("TonemapExposureBias", 1.0f);
        pass->AddShaderParameter("TonemapMaxWhite", 8.0f);
        postProcessPasses_.push_back(pass);
        break;
    }
    case PostProcessTonemapping::Uncharted2:
    {
        auto pass = MakeShared<SimplePostProcessPass>(this, renderBufferManager_,
            PostProcessPassFlag::NeedColorOutputReadAndWrite,
            BLEND_REPLACE, "v2/PP_Tonemap", "UNCHARTED2");
        pass->AddShaderParameter("TonemapExposureBias", 1.0f);
        pass->AddShaderParameter("TonemapMaxWhite", 4.0f);
        postProcessPasses_.push_back(pass);
        break;
    }
    default:
        break;
    }

    if (settings_.postProcess_.greyScale_)
    {
        auto pass = MakeShared<SimplePostProcessPass>(this, renderBufferManager_,
            PostProcessPassFlag::NeedColorOutputReadAndWrite,
            BLEND_REPLACE, "v2/PP_GreyScale", "");
        postProcessPasses_.push_back(pass);
    }
}

bool RenderPipeline::Define(RenderSurface* renderTarget, Viewport* viewport)
{
    // Lazy initialize heavy objects
    if (!drawQueue_)
    {
        drawQueue_ = MakeShared<DrawCommandQueue>(graphics_);
        renderBufferManager_ = MakeShared<RenderBufferManager>(this);
        shadowMapAllocator_ = MakeShared<ShadowMapAllocator>(context_);
        instancingBuffer_ = MakeShared<InstancingBuffer>(context_);
        sceneProcessor_ = MakeShared<SceneProcessor>(this, "shadow", shadowMapAllocator_, instancingBuffer_);
    }

    frameInfo_.viewport_ = viewport;
    frameInfo_.renderTarget_ = renderTarget;
    frameInfo_.viewportRect_ = viewport->GetEffectiveRect(renderTarget);
    frameInfo_.viewportSize_ = frameInfo_.viewportRect_.Size();

    if (!sceneProcessor_->Define(frameInfo_))
        return false;

    sceneProcessor_->SetRenderCamera(viewport->GetCamera());

    //settings_.deferredLighting_ = true;
    //settings_.enableInstancing_ = GetSubsystem<Renderer>()->GetDynamicInstancing();
    //settings_.ambientMode_ = DrawableAmbientMode::Directional;

    if (settingsDirty_)
    {
        settingsDirty_ = false;
        ValidateSettings();
        ApplySettings();
    }

    return true;
}

void RenderPipeline::Update(const FrameInfo& frameInfo)
{
    // Begin update. Should happen before pipeline state hash check.
    shadowMapAllocator_->ResetAllShadowMaps();
    OnUpdateBegin(this, frameInfo_);

    // Invalidate pipeline states if necessary
    MarkPipelineStateHashDirty();
    const unsigned pipelineStateHash = GetPipelineStateHash();
    if (oldPipelineStateHash_ != pipelineStateHash)
    {
        oldPipelineStateHash_ = pipelineStateHash;
        OnPipelineStatesInvalidated(this);
    }

    sceneProcessor_->Update();

    OnUpdateEnd(this, frameInfo_);
}

void RenderPipeline::Render()
{
    PostProcessPassFlags postProcessFlags;
    for (PostProcessPass* postProcessPass : postProcessPasses_)
        postProcessFlags |= postProcessPass->GetExecutionFlags();
    const bool needColorOutputBilinear = postProcessFlags.Test(PostProcessPassFlag::NeedColorOutputBilinear);
    const bool needColorOutputReadAndWrite = postProcessFlags.Test(PostProcessPassFlag::NeedColorOutputReadAndWrite);

    RenderBufferParams viewportParams{ Graphics::GetRGBFormat() };

    ViewportRenderBufferFlags viewportFlags;
    viewportFlags.Assign(ViewportRenderBufferFlag::SupportOutputColorReadWrite, needColorOutputReadAndWrite);
    viewportFlags.Assign(ViewportRenderBufferFlag::InheritBilinearFiltering, !needColorOutputBilinear);
    viewportParams.flags_.Assign(RenderBufferFlag::BilinearFiltering, needColorOutputBilinear);

    if (settings_.deferredLighting_)
    {
        viewportFlags |= ViewportRenderBufferFlag::UsableWithMultipleRenderTargets;
        viewportFlags |= ViewportRenderBufferFlag::IsReadableDepth;
        viewportFlags |= ViewportRenderBufferFlag::HasStencil;
    }
    else
    {
        viewportFlags |= ViewportRenderBufferFlag::InheritMultiSampleLevel;
    }
    renderBufferManager_->RequestViewport(viewportFlags, viewportParams);

    OnRenderBegin(this, frameInfo_);

    // TODO(renderer): Do something about this hack
    graphics_->SetVertexBuffer(nullptr);

    sceneProcessor_->RenderShadowMaps();

    auto sceneBatchRenderer_ = sceneProcessor_->GetBatchRenderer();
    const Color fogColor = sceneProcessor_->GetFrameInfo().camera_->GetEffectiveFogColor();

    // TODO(renderer): Remove this guard
#ifdef DESKTOP_GRAPHICS
    if (settings_.deferredLighting_)
    {
        //deferredFinal_ = renderBufferManager_->GetColorOutput();

        // Draw deferred GBuffer
        renderBufferManager_->ClearColor(deferredAlbedo_, Color::TRANSPARENT_BLACK);
        renderBufferManager_->ClearOutput(fogColor, 1.0f, 0);

        RenderBuffer* const gBuffer[] = {
            renderBufferManager_->GetColorOutput(),
            deferredAlbedo_,
            deferredNormal_
        };
        renderBufferManager_->SetRenderTargets(renderBufferManager_->GetDepthStencilOutput(), gBuffer);

        drawQueue_->Reset();

        instancingBuffer_->Begin();
        sceneBatchRenderer_->RenderBatches({ *drawQueue_, *sceneProcessor_->GetFrameInfo().camera_ },
            BatchRenderFlag::EnableAmbientLighting | BatchRenderFlag::EnableInstancingForStaticGeometry,
            deferredPass_->GetBatches());
        instancingBuffer_->End();

        drawQueue_->Execute();

        // Draw deferred lights
        const ShaderResourceDesc geometryBuffer[] = {
            { TU_ALBEDOBUFFER, deferredAlbedo_->GetTexture() },
            { TU_NORMALBUFFER, deferredNormal_->GetTexture() },
            { TU_DEPTHBUFFER, renderBufferManager_->GetDepthStencilTexture() }
        };

        const ShaderParameterDesc cameraParameters[] = {
            { VSP_GBUFFEROFFSETS, renderBufferManager_->GetOutputClipToUVSpaceOffsetAndScale() },
            { PSP_GBUFFERINVSIZE, renderBufferManager_->GetInvOutputSize() },
        };

        BatchRenderingContext ctx{ *drawQueue_, *sceneProcessor_->GetFrameInfo().camera_ };
        ctx.globalResources_ = geometryBuffer;
        ctx.cameraParameters_ = cameraParameters;

        drawQueue_->Reset();
        sceneBatchRenderer_->RenderLightVolumeBatches(ctx, sceneProcessor_->GetLightVolumeBatches());

        renderBufferManager_->SetOutputRenderTargers();
        drawQueue_->Execute();

        graphics_->SetDepthWrite(true);
    }
    else
#endif
    {
        renderBufferManager_->ClearOutput(fogColor, 1.0f, 0);
        renderBufferManager_->SetOutputRenderTargers();

        drawQueue_->Reset();
        instancingBuffer_->Begin();
        BatchRenderingContext ctx{ *drawQueue_, *sceneProcessor_->GetFrameInfo().camera_ };
        sceneBatchRenderer_->RenderBatches(ctx,
            BatchRenderFlag::EnableAmbientLighting | BatchRenderFlag::EnableVertexLights | BatchRenderFlag::EnablePixelLights | BatchRenderFlag::EnableInstancingForStaticGeometry, basePass_->GetSortedBaseBatches());
        sceneBatchRenderer_->RenderBatches(ctx,
            BatchRenderFlag::EnablePixelLights | BatchRenderFlag::EnableInstancingForStaticGeometry, basePass_->GetSortedLightBatches());
        sceneBatchRenderer_->RenderBatches(ctx,
            BatchRenderFlag::EnableAmbientLighting | BatchRenderFlag::EnableVertexLights | BatchRenderFlag::EnablePixelLights | BatchRenderFlag::EnableInstancingForStaticGeometry, alphaPass_->GetSortedBatches());
        instancingBuffer_->End();
        drawQueue_->Execute();
    }

    // Draw debug geometry if enabled
    auto debug = sceneProcessor_->GetFrameInfo().octree_->GetComponent<DebugRenderer>();
    if (debug && debug->IsEnabledEffective() && debug->HasContent())
    {
#if 0
        // If used resolve from backbuffer, blit first to the backbuffer to ensure correct depth buffer on OpenGL
        // Otherwise use the last rendertarget and blit after debug geometry
        if (usedResolve_ && currentRenderTarget_ != renderTarget_)
        {
            BlitFramebuffer(currentRenderTarget_->GetParentTexture(), renderTarget_, false);
            currentRenderTarget_ = renderTarget_;
            lastCustomDepthSurface_ = nullptr;
        }

        graphics_->SetRenderTarget(0, currentRenderTarget_);
        for (unsigned i = 1; i < MAX_RENDERTARGETS; ++i)
            graphics_->SetRenderTarget(i, (RenderSurface*)nullptr);

        // If a custom depth surface was used, use it also for debug rendering
        graphics_->SetDepthStencil(lastCustomDepthSurface_ ? lastCustomDepthSurface_ : GetDepthStencil(currentRenderTarget_));

        IntVector2 rtSizeNow = graphics_->GetRenderTargetDimensions();
        IntRect viewport = (currentRenderTarget_ == renderTarget_) ? viewRect_ : IntRect(0, 0, rtSizeNow.x_,
            rtSizeNow.y_);
        graphics_->SetViewport(viewport);

        debug->SetView(camera_);
        debug->Render();
#endif
    }

    for (PostProcessPass* postProcessPass : postProcessPasses_)
        postProcessPass->Execute();

    OnRenderEnd(this, frameInfo_);
}

unsigned RenderPipeline::RecalculatePipelineStateHash() const
{
    unsigned hash = 0;
    CombineHash(hash, graphics_->GetConstantBuffersEnabled());
    CombineHash(hash, sceneProcessor_->GetCameraProcessor()->GetPipelineStateHash());
    CombineHash(hash, settings_.CalculatePipelineStateHash());
    return hash;
}

}
