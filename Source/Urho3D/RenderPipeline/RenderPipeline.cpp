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

enum class SampleConversionType
{
    GammaToLinear,
    None,
    LinearToGamma,
    LinearToGammaSquare,
};

SampleConversionType GetSampleLinearConversion(bool linearInput, bool srgbTexture)
{
    static const SampleConversionType mapping[2][2] = {
        // linearInput = false
        {
            // srgbTexture = false
            SampleConversionType::GammaToLinear,
            // srgbTexture = true
            SampleConversionType::None
        },
        // linearInput = true
        {
            // srgbTexture = false
            SampleConversionType::None,
            // srgbTexture = true
            SampleConversionType::LinearToGamma
        },
    };
    return mapping[linearInput][srgbTexture];
}

SampleConversionType GetSampleGammaConversion(bool linearInput, bool srgbTexture)
{
    static const SampleConversionType mapping[3] = {
        SampleConversionType::None,
        SampleConversionType::LinearToGamma,
        SampleConversionType::LinearToGammaSquare
    };

    const auto sampleLinear = GetSampleLinearConversion(linearInput, srgbTexture);
    assert(sampleLinear != SampleConversionType::LinearToGammaSquare);
    return mapping[static_cast<int>(sampleLinear)];
}

const char* GetSampleConversionFunction(SampleConversionType conversion)
{
    switch (conversion)
    {
    case SampleConversionType::GammaToLinear:
        return "COLOR_GAMMATOLINEAR4 ";
    case SampleConversionType::LinearToGamma:
        return "COLOR_LINEARTOGAMMA4 ";
    case SampleConversionType::LinearToGammaSquare:
        return "COLOR_LINEARTOGAMMASQUARE4 ";
    case SampleConversionType::None:
    default:
        return "COLOR_NOOP ";
    }
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

Color GetDefaultFogColor(Graphics* graphics)
{
#ifdef URHO3D_OPENGL
    return graphics->GetForceGL2() ? Color::RED * 0.5f : Color::BLUE * 0.5f;
#else
    return Color::GREEN * 0.5f;
#endif
}

static const ea::vector<ea::string> ambientModeNames =
{
    "Constant",
    "Flat",
    "Directional",
};

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

    URHO3D_ENUM_ATTRIBUTE("Ambient Mode", settings_.ambientMode_, ambientModeNames, AmbientMode::Flat, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Enable Instancing", bool, settings_.enableInstancing_, false, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Enable Shadow", bool, settings_.enableShadows_, true, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Deferred Rendering", bool, settings_.deferred_, false, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Use Variance Shadow Maps", bool, settings_.varianceShadowMap_, false, AM_DEFAULT);
    URHO3D_ATTRIBUTE("VSM Shadow Settings", Vector2, settings_.vsmShadowParams_, SceneProcessorSettings::DefaultVSMShadowParams, AM_DEFAULT);
    URHO3D_ATTRIBUTE("VSM Multi Sample", int, settings_.varianceShadowMapMultiSample_, 1, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Gamma Correction", bool, settings_.gammaCorrection_, false, AM_DEFAULT);
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
        return CreateLightVolumePipelineState(key.light_, key.geometry_);
    }

    Geometry* geometry = key.geometry_;
    Material* material = key.material_;
    Pass* pass = key.pass_;
    Light* light = key.light_ ? key.light_->GetLight() : nullptr;
    const bool isLitBasePass = ctx.subpassIndex_ == 1;
    const bool isShadowPass = isInternalPass;

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
    // TODO(renderer): Don't use ShadowMapAllocator for this
    if (isShadowPass)
        sceneProcessor_->GetShadowMapAllocator()->ExportPipelineState(desc, light->GetShadowBias());

    // Add lightmap
    if (key.drawable_->GetBatches()[key.sourceBatchIndex_].lightmapScaleOffset_)
        commonDefines += "LIGHTMAP ";

    // Add ambient vertex defines
    switch (settings_.ambientMode_)
    {
    case AmbientMode::Constant:
        vertexShaderDefines += "URHO3D_AMBIENT_CONSTANT ";
        break;
    case AmbientMode::Flat:
        vertexShaderDefines += "URHO3D_AMBIENT_FLAT ";
        break;
    case AmbientMode::Directional:
        vertexShaderDefines += "URHO3D_AMBIENT_DIRECTIONAL ";
        break;
    default:
        break;
    }

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

    // Add material defines
    if (Texture* diffuseTexture = material->GetTexture(TU_DIFFUSE))
    {
        pixelShaderDefines += "MATERIAL_HAS_DIFFUSE_TEXTURE ";
        const bool isLinear = diffuseTexture->GetLinear();
        const bool sRGB = diffuseTexture->GetSRGB();
        pixelShaderDefines += "MATERIAL_DIFFUSE_TEXTURE_LINEAR=";
        pixelShaderDefines += GetSampleConversionFunction(GetSampleLinearConversion(isLinear, sRGB));
        pixelShaderDefines += "MATERIAL_DIFFUSE_TEXTURE_GAMMA=";
        pixelShaderDefines += GetSampleConversionFunction(GetSampleGammaConversion(isLinear, sRGB));
    }
    if (material->GetTexture(TU_NORMAL))
        pixelShaderDefines += "MATERIAL_HAS_NORMAL_TEXTURE ";
    if (material->GetTexture(TU_SPECULAR))
        pixelShaderDefines += "MATERIAL_HAS_SPECULAR_TEXTURE ";
    if (material->GetTexture(TU_EMISSIVE))
        pixelShaderDefines += "MATERIAL_HAS_EMISSIVE_TEXTURE ";

    // Add geometry type defines
    switch (key.geometryType_)
    {
    case GEOM_STATIC:
        if (settings_.enableInstancing_)
            vertexShaderDefines += "GEOM_INSTANCED ";
        else
            vertexShaderDefines += "GEOM_STATIC ";
        break;
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

    if (isShadowPass)
        commonDefines += "PASS_SHADOW ";
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
        if (key.light_->HasShadow())
        {
            if (settings_.varianceShadowMap_)
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
    const CullMode materialCullMode = isShadowPass ? material->GetShadowCullMode() : material->GetCullMode();
    const CullMode cullMode = passCullMode != MAX_CULLMODES ? passCullMode : materialCullMode;
    desc.cullMode_ = isShadowPass ? cullMode : GetEffectiveCullMode(cullMode, sceneProcessor_->GetFrameInfo().cullCamera_);

    if (ctx.pass_ == deferredPass_)
    {
        desc.stencilEnabled_ = true;
        desc.stencilPass_ = OP_REF;
        desc.writeMask_ = PORTABLE_LIGHTMASK;
        desc.stencilRef_ = key.drawable_->GetLightMaskInZone() & PORTABLE_LIGHTMASK;
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
        if (settings_.varianceShadowMap_)
            pixelDefiles += "SHADOW VSM_SHADOW ";
        else
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

    if (type != LIGHT_DIRECTIONAL)
    {
        if (sceneLight->DoesOverlapCamera())
        {
            desc.cullMode_ = GetEffectiveCullMode(CULL_CW, sceneProcessor_->GetFrameInfo().camera_);
            desc.depthMode_ = CMP_GREATER;
        }
        else
        {
            desc.cullMode_ = GetEffectiveCullMode(CULL_CCW, sceneProcessor_->GetFrameInfo().camera_);
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

    desc.stencilEnabled_ = true;
    desc.stencilMode_ = CMP_NOTEQUAL;
    desc.compareMask_ = light->GetLightMaskEffective() & PORTABLE_LIGHTMASK;
    desc.stencilRef_ = 0;

    return renderer_->GetOrCreatePipelineState(desc);
}

bool RenderPipeline::Define(RenderSurface* renderTarget, Viewport* viewport)
{
    // Lazy initialize heavy objects
    if (!drawQueue_)
    {
        drawQueue_ = MakeShared<DrawCommandQueue>(graphics_);
        sceneProcessor_ = MakeShared<SceneProcessor>(this, "shadow");
        cameraProcessor_ = MakeShared<CameraProcessor>(this);

        viewportColor_ = RenderBuffer::ConnectToViewportColor(this);
        viewportDepth_ = RenderBuffer::ConnectToViewportDepthStencil(this);
    }

    sceneProcessor_->Define(renderTarget, viewport);
    if (!sceneProcessor_->IsValid())
        return false;

    cameraProcessor_->Initialize(viewport->GetCamera());

    //settings_.enableInstancing_ = GetSubsystem<Renderer>()->GetDynamicInstancing();

    // TODO(renderer): Optimize this
    previousSettings_ = settings_;

    // Validate settings
    if (settings_.deferred_ && !graphics_->GetDeferredSupport()
        && !Graphics::GetReadableDepthStencilFormat())
        settings_.deferred_ = false;

    if (!settings_.lowPrecisionShadowMaps_ && !graphics_->GetHiresShadowMapFormat())
        settings_.lowPrecisionShadowMaps_ = true;

    if (settings_.enableInstancing_ && !graphics_->GetInstancingSupport())
        settings_.enableInstancing_ = false;

    sceneProcessor_->SetSettings(settings_);

    // Initialize or reset deferred rendering
    // TODO(renderer): Make it more clean
    if (settings_.deferred_ && !deferredPass_)
    {
        basePass_ = nullptr;
        alphaPass_ = nullptr;
        deferredPass_ = MakeShared<UnlitScenePass>(this, sceneProcessor_->GetDrawableProcessor(), "deferred");

        deferredFinal_ = RenderBuffer::Create(this, RenderBufferFlag::RGBA);
        deferredAlbedo_ = RenderBuffer::Create(this, RenderBufferFlag::RGBA);
        deferredNormal_ = RenderBuffer::Create(this, RenderBufferFlag::RGBA);
        deferredDepth_ = RenderBuffer::Create(this, RenderBufferFlag::Depth | RenderBufferFlag::Stencil);

        sceneProcessor_->SetPasses({ deferredPass_ });
    }

    if (!settings_.deferred_ && !basePass_)
    {
        basePass_ = MakeShared<OpaqueForwardLightingScenePass>(this, sceneProcessor_->GetDrawableProcessor(), "base", "litbase", "light");
        alphaPass_ = MakeShared<AlphaForwardLightingScenePass>(this, sceneProcessor_->GetDrawableProcessor(), "alpha", "alpha", "litalpha");
        deferredPass_ = nullptr;

        deferredFinal_ = nullptr;
        deferredAlbedo_ = nullptr;
        deferredNormal_ = nullptr;
        deferredDepth_ = nullptr;

        sceneProcessor_->SetPasses({ basePass_, alphaPass_ });
    }

    return true;
}

void RenderPipeline::Update(const FrameInfo& frameInfo)
{
    // Begin update. Should happen before pipeline state hash check.
    sceneProcessor_->UpdateFrameInfo(frameInfo);
    OnUpdateBegin(this, sceneProcessor_->GetFrameInfo());

    // Invalidate pipeline states if necessary
    MarkPipelineStateHashDirty();
    const unsigned pipelineStateHash = GetPipelineStateHash();
    if (oldPipelineStateHash_ != pipelineStateHash)
    {
        oldPipelineStateHash_ = pipelineStateHash;
        OnPipelineStatesInvalidated(this);
    }

    sceneProcessor_->Update();

    OnUpdateEnd(this, sceneProcessor_->GetFrameInfo());
}

void RenderPipeline::Render()
{
    OnRenderBegin(this, sceneProcessor_->GetFrameInfo());

    // TODO(renderer): Do something about this hack
    graphics_->SetVertexBuffer(nullptr);

    sceneProcessor_->RenderShadowMaps();

    auto sceneBatchRenderer_ = sceneProcessor_->GetBatchRenderer();
    auto instancingBuffer = sceneProcessor_->GetInstancingBuffer();
    auto flag = settings_.enableInstancing_ ? BatchRenderFlag::InstantiateStaticGeometry : BatchRenderFlag::None;

    // TODO(renderer): Remove this guard
#ifdef DESKTOP_GRAPHICS
    if (settings_.deferred_)
    {
        // Draw deferred GBuffer
        deferredFinal_->ClearColor(GetDefaultFogColor(graphics_));
        deferredAlbedo_->ClearColor();
        deferredDepth_->ClearDepthStencil();
        deferredDepth_->SetRenderTargets({ deferredFinal_, deferredAlbedo_, deferredNormal_ });
        drawQueue_->Reset();
        if (settings_.enableInstancing_)
            instancingBuffer->Reset();
        sceneBatchRenderer_->SetInstancingBuffer(instancingBuffer);
        sceneBatchRenderer_->RenderBatches(*drawQueue_, sceneProcessor_->GetFrameInfo().camera_,
            BatchRenderFlag::AmbientLight | flag, deferredPass_->GetBatches());
        if (settings_.enableInstancing_)
            instancingBuffer->Commit();
        drawQueue_->Execute();

        // Draw deferred lights
        ShaderResourceDesc geometryBuffer[] = {
            { TU_ALBEDOBUFFER, deferredAlbedo_->GetRenderSurface()->GetParentTexture() },
            { TU_NORMALBUFFER, deferredNormal_->GetRenderSurface()->GetParentTexture() },
            { TU_DEPTHBUFFER, deferredDepth_->GetRenderSurface()->GetParentTexture() }
        };

        LightVolumeRenderContext lightVolumeRenderContext;
        lightVolumeRenderContext.geometryBuffer_ = geometryBuffer;
        lightVolumeRenderContext.geometryBufferOffsetAndScale_ = deferredDepth_->GetViewportOffsetAndScale();
        lightVolumeRenderContext.geometryBufferInvSize_ = deferredDepth_->GetInvSize();

        drawQueue_->Reset();
        sceneBatchRenderer_->RenderLightVolumeBatches(*drawQueue_, sceneProcessor_->GetFrameInfo().camera_,
            lightVolumeRenderContext, sceneProcessor_->GetLightVolumeBatches());

        deferredDepth_->SetRenderTargets({ deferredFinal_ });
        drawQueue_->Execute();

        viewportColor_->CopyFrom(deferredFinal_);
    }
    else
#endif
    {
        viewportColor_->ClearColor(GetDefaultFogColor(graphics_));
        viewportDepth_->ClearDepthStencil();
        viewportDepth_->SetRenderTargets({ viewportColor_ });

        drawQueue_->Reset();
        if (settings_.enableInstancing_)
            instancingBuffer->Reset();
        sceneBatchRenderer_->SetInstancingBuffer(instancingBuffer);
        sceneBatchRenderer_->RenderBatches(*drawQueue_, sceneProcessor_->GetFrameInfo().camera_,
            BatchRenderFlag::AmbientLight | BatchRenderFlag::VertexLights | BatchRenderFlag::PixelLight | flag, basePass_->GetSortedBaseBatches());
        sceneBatchRenderer_->RenderBatches(*drawQueue_, sceneProcessor_->GetFrameInfo().camera_,
            BatchRenderFlag::PixelLight | flag, basePass_->GetSortedLightBatches());
        sceneBatchRenderer_->RenderBatches(*drawQueue_, sceneProcessor_->GetFrameInfo().camera_,
            BatchRenderFlag::AmbientLight | BatchRenderFlag::VertexLights | BatchRenderFlag::PixelLight | flag, alphaPass_->GetSortedBatches());
        if (settings_.enableInstancing_)
            instancingBuffer->Commit();
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
    OnRenderEnd(this, sceneProcessor_->GetFrameInfo());
}

unsigned RenderPipeline::RecalculatePipelineStateHash() const
{
    unsigned hash = 0;
    CombineHash(hash, cameraProcessor_->GetPipelineStateHash());
    CombineHash(hash, static_cast<unsigned>(settings_.ambientMode_));
    CombineHash(hash, settings_.gammaCorrection_);
    CombineHash(hash, settings_.enableInstancing_);
    CombineHash(hash, settings_.enableShadows_);
    CombineHash(hash, settings_.deferred_);
    CombineHash(hash, settings_.varianceShadowMap_);
    return hash;
}

}
