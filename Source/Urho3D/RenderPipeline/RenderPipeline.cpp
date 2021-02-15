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
#include "../Graphics/DebugRenderer.h"
#include "../Graphics/DrawCommandQueue.h"
#include "../Graphics/IndexBuffer.h"
#include "../Graphics/OcclusionBuffer.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/Viewport.h"
#include "../RenderPipeline/RenderPipeline.h"
#include "../RenderPipeline/DrawableProcessor.h"
#include "../RenderPipeline/SceneBatchCollector.h"
#include "../RenderPipeline/SceneBatchRenderer.h"
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

/// %Frustum octree query for occluders.
class OccluderOctreeQuery : public FrustumOctreeQuery
{
public:
    /// Construct with frustum and query parameters.
    OccluderOctreeQuery(ea::vector<Drawable*>& result, const Frustum& frustum, unsigned viewMask = DEFAULT_VIEWMASK) :
        FrustumOctreeQuery(result, frustum, DRAWABLE_GEOMETRY, viewMask)
    {
    }

    /// Intersection test for drawables.
    void TestDrawables(Drawable** start, Drawable** end, bool inside) override
    {
        for (Drawable* drawable : MakeIteratorRange(start, end))
        {
            const DrawableFlags flags = drawable->GetDrawableFlags();
            if (flags == DRAWABLE_GEOMETRY && drawable->IsOccluder() && (drawable->GetViewMask() & viewMask_))
            {
                if (inside || frustum_.IsInsideFast(drawable->GetWorldBoundingBox()))
                    result_.push_back(drawable);
            }
        }
    }
};

/// %Frustum octree query with occlusion.
class OccludedFrustumOctreeQuery : public FrustumOctreeQuery
{
public:
    /// Construct with frustum, occlusion buffer and query parameters.
    OccludedFrustumOctreeQuery(ea::vector<Drawable*>& result, const Frustum& frustum, OcclusionBuffer* buffer,
                               DrawableFlags drawableFlags = DRAWABLE_ANY, unsigned viewMask = DEFAULT_VIEWMASK) :
        FrustumOctreeQuery(result, frustum, drawableFlags, viewMask),
        buffer_(buffer)
    {
    }

    /// Intersection test for an octant.
    Intersection TestOctant(const BoundingBox& box, bool inside) override
    {
        if (inside)
            return buffer_->IsVisible(box) ? INSIDE : OUTSIDE;
        else
        {
            Intersection result = frustum_.IsInside(box);
            if (result != OUTSIDE && !buffer_->IsVisible(box))
                result = OUTSIDE;
            return result;
        }
    }

    /// Intersection test for drawables. Note: drawable occlusion is performed later in worker threads.
    void TestDrawables(Drawable** start, Drawable** end, bool inside) override
    {
        for (Drawable* drawable : MakeIteratorRange(start, end))
        {
            if ((drawable->GetDrawableFlags() & drawableFlags_) && (drawable->GetViewMask() & viewMask_))
            {
                if (inside || frustum_.IsInsideFast(drawable->GetWorldBoundingBox()))
                    result_.push_back(drawable);
            }
        }
    }

    /// Occlusion buffer.
    OcclusionBuffer* buffer_;
};

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
    URHO3D_ATTRIBUTE("Deferred Rendering", bool, settings_.deferred_, false, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Gamma Correction", bool, settings_.gammaCorrection_, false, AM_DEFAULT);
}

void RenderPipeline::ApplyAttributes()
{
}

SharedPtr<PipelineState> RenderPipeline::CreateBatchPipelineState(
    const BatchStateCreateKey& key, const BatchStateCreateContext& ctx)
{
    const bool isInternalPass = sceneProcessor_->IsInternalPass(ctx.pass_);
    if (isInternalPass && ctx.subpassIndex_ == 1)
    {
        return CreateLightVolumePipelineState(key.light_, key.geometry_);
    }

    Geometry* geometry = key.geometry_;
    Material* material = key.material_;
    Pass* pass = key.pass_;
    Light* light = key.light_ ? key.light_->GetLight() : nullptr;
    const bool isLitBasePass = ctx.subpassIndex_ == 1;
    const bool isShadowPass = isInternalPass;

    // TODO(renderer): Remove this hack
    if (!pass)
        return nullptr;

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
    // TODO(renderer): Get batch index as input?
    if (key.drawable_->GetBatches()[0].lightmapScaleOffset_)
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

    if (isLitBasePass)
        commonDefines += "NUMVERTEXLIGHTS=4 ";

    if (light)
    {
        commonDefines += "PERPIXEL ";
        if (key.light_->HasShadow())
        {
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

bool RenderPipeline::HasShadow(Light* light) { return IsLightShadowed(light); }

bool RenderPipeline::IsLightShadowed(Light* light)
{
    const bool shadowsEnabled = renderer_->GetDrawShadows()
        && light->GetCastShadows()
        && light->GetLightImportance() != LI_NOT_IMPORTANT
        && light->GetShadowIntensity() < 1.0f;

    if (!shadowsEnabled)
        return false;

    if (light->GetShadowDistance() > 0.0f && light->GetDistance() > light->GetShadowDistance())
        return false;

    return true;
}

/*ShadowMap RenderPipeline::AllocateTransientShadowMap(const IntVector2& size)
{
    return shadowMapAllocator_->AllocateShadowMap(size);
}

ShadowMap RenderPipeline::GetTemporaryShadowMap(const IntVector2& size)
{
    return shadowMapAllocator_->AllocateShadowMap(size);
}*/

bool RenderPipeline::Define(RenderSurface* renderTarget, Viewport* viewport)
{
    if (!sceneProcessor_)
    {
        sceneProcessor_ = MakeShared<SceneProcessor>(this, "shadow");
    }

    sceneProcessor_->Define(renderTarget, viewport);
    if (!sceneProcessor_->IsValid())
        return false;

    Camera* camera = viewport->GetCamera();

    /*frameInfo_.viewport_ = viewport;
    frameInfo_.renderTarget_ = renderTarget;

    frameInfo_.scene_ = viewport->GetScene();
    frameInfo_.camera_ = viewport->GetCamera();
    frameInfo_.cullCamera_ = viewport->GetCullCamera() ? viewport->GetCullCamera() : frameInfo_.camera_;
    frameInfo_.octree_ = frameInfo_.scene_ ? frameInfo_.scene_->GetComponent<Octree>() : nullptr;

    frameInfo_.viewRect_ = viewport->GetEffectiveRect(renderTarget);
    frameInfo_.viewSize_ = frameInfo_.viewRect_.Size();

    if (!frameInfo_.camera_ || !frameInfo_.octree_)
        return false;*/

    // TODO(renderer): Optimize this
    previousSettings_ = settings_;
    sceneProcessor_->SetSettings(settings_);

    // Validate settings
    if (settings_.deferred_ && !graphics_->GetDeferredSupport()
        && !Graphics::GetReadableDepthStencilFormat())
        settings_.deferred_ = false;

    // Lazy initialize heavy objects
    if (!drawQueue_)
    {
        drawQueue_ = MakeShared<DrawCommandQueue>(graphics_);
        //sceneProcessor_ = MakeShared<SceneProcessor>(this, "shadow");

        cameraProcessor_ = MakeShared<CameraProcessor>(this);
        viewportColor_ = RenderBuffer::ConnectToViewportColor(this);// MakeShared<ViewportColorRenderBuffer>(this);
        viewportDepth_ = RenderBuffer::ConnectToViewportDepthStencil(this);// MakeShared<ViewportDepthStencilRenderBuffer>(this);

        //drawableProcessor_ = MakeShared<DrawableProcessor>(this);
        //batchCompositor_ = MakeShared<BatchCompositor>(this, drawableProcessor_, Technique::GetPassIndex("shadow"));
        //sceneBatchCollector_ = MakeShared<SceneBatchCollector>(context_, drawableProcessor_, batchCompositor_, this);

        //shadowMapAllocator_ = MakeShared<ShadowMapAllocator>(context_);
        sceneBatchRenderer_ = MakeShared<SceneBatchRenderer>(context_, sceneProcessor_->GetDrawableProcessor());
    }

    // Pre-frame initialize objects
    cameraProcessor_->Initialize(camera);

    // Initialize or reset deferred rendering
    // TODO(renderer): Make it more clean
    if (settings_.deferred_ && !deferredPass_)
    {
        basePass_ = nullptr;
        alphaPass_ = nullptr;
        deferredPass_ = MakeShared<UnlitScenePass>(this, sceneProcessor_->GetDrawableProcessor(), "PASS_DEFERRED", "deferred");

        deferredFinal_ = RenderBuffer::Create(this, RenderBufferFlag::RGBA);
        deferredAlbedo_ = RenderBuffer::Create(this, RenderBufferFlag::RGBA);
        deferredNormal_ = RenderBuffer::Create(this, RenderBufferFlag::RGBA);
        deferredDepth_ = RenderBuffer::Create(this, RenderBufferFlag::Depth | RenderBufferFlag::Stencil);

        sceneProcessor_->SetPasses({ deferredPass_ });
        //batchCompositor_->SetPasses({ deferredPass_ });

        //sceneBatchCollector_->ResetPasses();
        //sceneBatchCollector_->AddScenePass(deferredPass_);
    }

    if (!settings_.deferred_ && !basePass_)
    {
        basePass_ = MakeShared<OpaqueForwardLightingScenePass>(this, sceneProcessor_->GetDrawableProcessor(), "PASS_BASE", "base", "litbase", "light");
        alphaPass_ = MakeShared<AlphaForwardLightingScenePass>(this, sceneProcessor_->GetDrawableProcessor(), "PASS_ALPHA", "alpha", "alpha", "litalpha");
        deferredPass_ = nullptr;

        deferredFinal_ = nullptr;
        deferredAlbedo_ = nullptr;
        deferredNormal_ = nullptr;
        deferredDepth_ = nullptr;

        sceneProcessor_->SetPasses({ basePass_, alphaPass_ });
        //batchCompositor_->SetPasses({ basePass_, alphaPass_ });

        //sceneBatchCollector_->ResetPasses();
        //sceneBatchCollector_->AddScenePass(basePass_);
        //sceneBatchCollector_->AddScenePass(alphaPass_);
    }

    return true;
}

void RenderPipeline::Update(const FrameInfo& frameInfo)
{
    // Invalidate pipeline states if necessary
    MarkPipelineStateHashDirty();
    const unsigned pipelineStateHash = GetPipelineStateHash();
    if (oldPipelineStateHash_ != pipelineStateHash)
    {
        oldPipelineStateHash_ = pipelineStateHash;
        OnPipelineStatesInvalidated(this);
    }

    // Update scene and batches
    sceneProcessor_->UpdateFrameInfo(frameInfo);
    // TODO(renderer): Move to event
    //shadowMapAllocator_->Reset();

    // Setup frame info
    /*frameInfo_.timeStep_ = frameInfo.timeStep_;
    frameInfo_.frameNumber_ = frameInfo.frameNumber_;
    frameInfo_.numThreads_ = workQueue_->GetNumThreads() + 1;*/

    // Begin update event
    OnUpdateBegin(this, sceneProcessor_->GetFrameInfo());
    sceneProcessor_->Update();

    // Collect occluders
    // TODO(renderer): Make optional
    /*OccluderOctreeQuery occluderQuery(occluders_,
        frameInfo_.cullCamera_->GetFrustum(), frameInfo_.cullCamera_->GetViewMask());
    frameInfo_.octree_->GetDrawables(occluderQuery);

    // Collect visible drawables
    // TODO(renderer): Add occlusion culling
    FrustumOctreeQuery drawableQuery(drawables_, frameInfo_.cullCamera_->GetFrustum(),
        DRAWABLE_GEOMETRY | DRAWABLE_LIGHT, frameInfo_.cullCamera_->GetViewMask());
    frameInfo_.octree_->GetDrawables(drawableQuery);*/

    // End update event
    OnUpdateEnd(this, sceneProcessor_->GetFrameInfo());
}

void RenderPipeline::Render()
{
    OnRenderBegin(this, sceneProcessor_->GetFrameInfo());

    // TODO(renderer): Do something about this hack
    graphics_->SetVertexBuffer(nullptr);

    //viewport_->BeginFrame();
    //shadowMapAllocator_->Reset();

    // Collect and process occluders
    // TODO(renderer): Fix me
#if 0
    const unsigned maxOccluderTriangles_ = renderer_->GetMaxOccluderTriangles();
    unsigned activeOccluders_ = 0;
    if (maxOccluderTriangles_ > 0 && occluders.size() > 0)
    {
        //UpdateOccluders(occluders_, cullCamera_);
        occlusionBuffer_ = MakeShared<OcclusionBuffer>(context_);
        occlusionBuffer_->SetSize(256, 256, true);
        occlusionBuffer_->SetView(frameInfo_.cullCamera_);
        occlusionBuffer_->SetMaxTriangles((unsigned)maxOccluderTriangles_);
        occlusionBuffer_->Clear();

        if (!occlusionBuffer_->IsThreaded())
        {
            // If not threaded, draw occluders one by one and test the next occluder against already rasterized depth
            for (unsigned i = 0; i < occluders.size(); ++i)
            {
                Drawable* occluder = occluders[i];
                if (i > 0)
                {
                    // For subsequent occluders, do a test against the pixel-level occlusion occlusionBuffer_ to see if rendering is necessary
                    if (!occlusionBuffer_->IsVisible(occluder->GetWorldBoundingBox()))
                        continue;
                }

                // Check for running out of triangles
                ++activeOccluders_;
                bool success = occluder->DrawOcclusion(occlusionBuffer_);
                // Draw triangles submitted by this occluder
                occlusionBuffer_->DrawTriangles();
                if (!success)
                    break;
            }
        }
        else
        {
            // In threaded mode submit all triangles first, then render (cannot test in this case)
            for (unsigned i = 0; i < occluders.size(); ++i)
            {
                // Check for running out of triangles
                ++activeOccluders_;
                if (!occluders[i]->DrawOcclusion(occlusionBuffer_))
                    break;
            }

            occlusionBuffer_->DrawTriangles();
        }

        // Finally build the depth mip levels
        occlusionBuffer_->BuildDepthHierarchy();
    }
#endif

    // Process batches
    /*static ScenePassDescription passes[] = {
        { ScenePassType::ForwardLitBase,   "base",  "litbase",  "light" },
        { ScenePassType::ForwardUnlitBase, "alpha", "",         "litalpha" },
        { ScenePassType::Unlit, "postopaque" },
        { ScenePassType::Unlit, "refract" },
        { ScenePassType::Unlit, "postalpha" },
    };*/
    /*sceneBatchCollector_->SetMaxPixelLights(4);

    sceneBatchCollector_->BeginFrame(frameInfo_, *this);
    sceneBatchCollector_->ProcessVisibleDrawables(drawables_);
    sceneBatchCollector_->ProcessVisibleLights();
    sceneBatchCollector_->UpdateGeometries();
    sceneBatchCollector_->CollectSceneBatches();
    if (settings_.deferred_)
        sceneBatchCollector_->CollectLightVolumeBatches();*/

    // Collect batches
    static thread_local ea::vector<PipelineBatchByState> shadowBatches;
    const auto& visibleLights = sceneProcessor_->GetDrawableProcessor()->GetLightProcessors();
    for (LightProcessor* sceneLight : visibleLights)
    {
        const unsigned numSplits = sceneLight->GetNumSplits();
        for (unsigned splitIndex = 0; splitIndex < numSplits; ++splitIndex)
        {
            const ShadowSplitProcessor* split = sceneLight->GetSplit(splitIndex);
            split->SortShadowBatches(shadowBatches);

            drawQueue_->Reset();
            sceneBatchRenderer_->RenderBatches(*drawQueue_, split->GetShadowCamera(),
                BatchRenderFlag::None, shadowBatches);
            sceneProcessor_->GetShadowMapAllocator()->BeginShadowMap(split->GetShadowMap());
            drawQueue_->Execute();
        }
    }

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
        sceneBatchRenderer_->RenderBatches(*drawQueue_, sceneProcessor_->GetFrameInfo().camera_,
            BatchRenderFlag::AmbientAndVertexLights, deferredPass_->GetBatches());
        drawQueue_->Execute();

        // Draw deferred lights
        ShaderResourceDesc geometryBuffer[] = {
            { TU_ALBEDOBUFFER, deferredAlbedo_->GetRenderSurface()->GetParentTexture() },
            { TU_NORMALBUFFER, deferredNormal_->GetRenderSurface()->GetParentTexture() },
            { TU_DEPTHBUFFER, deferredDepth_->GetRenderSurface()->GetParentTexture() }
        };

        drawQueue_->Reset();
        sceneBatchRenderer_->RenderLightVolumeBatches(*drawQueue_, sceneProcessor_->GetFrameInfo().camera_,
            sceneProcessor_->GetLightVolumeBatches(), geometryBuffer,
            deferredDepth_->GetViewportOffsetAndScale(), deferredDepth_->GetInvSize());

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
        sceneBatchRenderer_->RenderBatches(*drawQueue_, sceneProcessor_->GetFrameInfo().camera_,
            BatchRenderFlag::AmbientAndVertexLights | BatchRenderFlag::PixelLight, basePass_->GetSortedLitBaseBatches());
        sceneBatchRenderer_->RenderBatches(*drawQueue_, sceneProcessor_->GetFrameInfo().camera_,
            BatchRenderFlag::PixelLight, basePass_->GetSortedLightBatches());
        sceneBatchRenderer_->RenderBatches(*drawQueue_, sceneProcessor_->GetFrameInfo().camera_,
            BatchRenderFlag::AmbientAndVertexLights | BatchRenderFlag::PixelLight, alphaPass_->GetSortedBatches());
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

/*SharedPtr<RenderBuffer> RenderPipeline::CreateScreenBuffer(
    const TextureRenderBufferParams& params, const Vector2& sizeMultiplier)
{
    return MakeShared<TextureRenderBuffer>(this, params, sizeMultiplier, IntVector2::ZERO, false);
}

SharedPtr<RenderBuffer> RenderPipeline::CreateFixedScreenBuffer(
    const TextureRenderBufferParams& params, const IntVector2& fixedSize)
{
    return MakeShared<TextureRenderBuffer>(this, params, Vector2::ONE, fixedSize, false);
}

SharedPtr<RenderBuffer> RenderPipeline::CreatePersistentScreenBuffer(
    const TextureRenderBufferParams& params, const Vector2& sizeMultiplier)
{
    return MakeShared<TextureRenderBuffer>(this, params, sizeMultiplier, IntVector2::ZERO, true);
}

SharedPtr<RenderBuffer> RenderPipeline::CreatePersistentFixedScreenBuffer(
    const TextureRenderBufferParams& params, const IntVector2& fixedSize)
{
    return MakeShared<TextureRenderBuffer>(this, params, Vector2::ONE, fixedSize, true);
}*/

unsigned RenderPipeline::RecalculatePipelineStateHash() const
{
    unsigned hash = 0;
    CombineHash(hash, cameraProcessor_->GetPipelineStateHash());
    CombineHash(hash, static_cast<unsigned>(settings_.ambientMode_));
    CombineHash(hash, settings_.deferred_);
    CombineHash(hash, settings_.gammaCorrection_);
    return hash;
}

}
