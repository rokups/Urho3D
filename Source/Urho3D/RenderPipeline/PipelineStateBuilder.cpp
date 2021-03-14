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

#include "../Graphics/Graphics.h"
#include "../Graphics/Renderer.h"
#include "../IO/Log.h"
#include "../RenderPipeline/CameraProcessor.h"
#include "../RenderPipeline/InstancingBuffer.h"
#include "../RenderPipeline/LightProcessor.h"
#include "../RenderPipeline/PipelineStateBuilder.h"
#include "../RenderPipeline/SceneProcessor.h"
#include "../RenderPipeline/ShadowMapAllocator.h"

#include <EASTL/span.h>

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

int GetTextureColorSpaceHint(bool linearInput, bool srgbTexture)
{
    return static_cast<int>(linearInput) + static_cast<int>(srgbTexture);
}

CullMode GetEffectiveCullMode(CullMode mode, bool isCameraReversed)
{
    if (mode == CULL_NONE || !isCameraReversed)
        return mode;

    return mode == CULL_CW ? CULL_CCW : CULL_CW;
}

CullMode GetEffectiveCullMode(CullMode passCullMode, CullMode materialCullMode, bool isCameraReversed)
{
    const CullMode cullMode = passCullMode != MAX_CULLMODES ? passCullMode : materialCullMode;
    return GetEffectiveCullMode(cullMode, isCameraReversed);
}

enum class VertexLayoutFlag
{
    HasNormal    = 1 << 0,
    HasTangent   = 1 << 1,
    HasColor     = 1 << 2,
    HasTexCoord0 = 1 << 3,
    HasTexCoord1 = 1 << 4,
    DefaultMask  = HasNormal | HasTangent | HasColor | HasTexCoord0 | HasTexCoord1,
    ShadowMask   = HasNormal | HasTexCoord0,
};

URHO3D_FLAGSET(VertexLayoutFlag, VertexLayoutFlags);

VertexLayoutFlags ScanVertexElements(ea::span<const VertexElement> elements)
{
    VertexLayoutFlags result;
    for (const VertexElement& element : elements)
    {
        switch (element.semantic_)
        {
        case SEM_NORMAL:
            result |= VertexLayoutFlag::HasNormal;
            break;

        case SEM_TANGENT:
            result |= VertexLayoutFlag::HasTangent;
            break;

        case SEM_COLOR:
            result |= VertexLayoutFlag::HasColor;
            break;

        case SEM_TEXCOORD:
            switch (element.index_)
            {
            case 0:
                result |= VertexLayoutFlag::HasTexCoord0;
                break;

            case 1:
                result |= VertexLayoutFlag::HasTexCoord1;
                break;

            default:
                break;
            }

        default:
            break;
        }
    }
    return result;
}

}

PipelineStateBuilder::PipelineStateBuilder(Context* context,
    const SceneProcessor* sceneProcessor, const CameraProcessor* cameraProcessor,
    const ShadowMapAllocator* shadowMapAllocator, const InstancingBuffer* instancingBuffer)
    : Object(context)
    , sceneProcessor_(sceneProcessor)
    , cameraProcessor_(cameraProcessor)
    , shadowMapAllocator_(shadowMapAllocator)
    , instancingBuffer_(instancingBuffer)
    , graphics_(GetSubsystem<Graphics>())
    , renderer_(GetSubsystem<Renderer>())
{
}

SharedPtr<PipelineState> PipelineStateBuilder::CreateBatchPipelineState(
    const BatchStateCreateKey& key, const BatchStateCreateContext& ctx)
{
    const BatchCompositorPass* batchCompositorPass = sceneProcessor_->GetUserPass(ctx.pass_);
    const bool isShadowPass = batchCompositorPass == nullptr && ctx.subpassIndex_ == BatchCompositor::ShadowSubpass;
    const bool isLightVolumePass = batchCompositorPass == nullptr && ctx.subpassIndex_ == BatchCompositor::LitVolumeSubpass;
    const bool isInstancingEnabled = isShadowPass
        || (batchCompositorPass && !batchCompositorPass->GetFlags().Test(DrawableProcessorPassFlag::DisableInstancing));

    ClearState();

    ApplyCommonDefines(key.pass_);
    ApplyGeometry(key.geometry_, key.geometryType_, isInstancingEnabled);
    ApplyVertexLayout(isShadowPass);
    if (isShadowPass)
        ApplyShadowPass(ctx.shadowSplitIndex_, key.pixelLight_, key.material_, key.pass_);
    else if (isLightVolumePass)
        ApplyLightVolumePass(key.pixelLight_);
    else if (batchCompositorPass)
        ApplyUserPass(batchCompositorPass, ctx.subpassIndex_, key.material_, key.pass_, key.drawable_);

    if (!isShadowPass && key.pixelLight_)
        ApplyPixelLight(key.pixelLight_, key.material_);

    FinalizeDescription(key.pass_);
    return renderer_->GetOrCreatePipelineState(desc_);
}

void PipelineStateBuilder::ClearState()
{
    commonDefines_.clear();
    vertexDefines_.clear();
    pixelDefines_.clear();
    desc_ = {};
}

void PipelineStateBuilder::ApplyCommonDefines(const Pass* materialPass)
{
    if (graphics_->GetCaps().constantBuffersSupported_)
        commonDefines_ += "URHO3D_USE_CBUFFERS ";

    if (sceneProcessor_->GetSettings().gammaCorrection_)
        commonDefines_ += "URHO3D_GAMMA_CORRECTION ";

    vertexDefines_ += materialPass->GetEffectiveVertexShaderDefines();
    vertexDefines_ += " ";
    pixelDefines_ += materialPass->GetEffectivePixelShaderDefines();
    pixelDefines_ += " ";
}

void PipelineStateBuilder::ApplyGeometry(Geometry* geometry, GeometryType geometryType, bool useInstancingBuffer)
{
    if (!useInstancingBuffer || !instancingBuffer_->IsEnabled())
        desc_.InitializeInputLayoutAndPrimitiveType(geometry);
    else
    {
        vertexDefines_ += "URHO3D_INSTANCING ";
        desc_.InitializeInputLayoutAndPrimitiveType(geometry, instancingBuffer_->GetVertexBuffer());
    }

    static const ea::string geometryDefines[] = {
        "URHO3D_GEOMETRY_STATIC ",
        "URHO3D_GEOMETRY_SKINNED ",
        "URHO3D_GEOMETRY_STATIC ",
        "URHO3D_GEOMETRY_BILLBOARD ",
        "URHO3D_GEOMETRY_DIRBILLBOARD ",
        "URHO3D_GEOMETRY_TRAIL_FACE_CAMERA ",
        "URHO3D_GEOMETRY_TRAIL_BONE ",
        "URHO3D_GEOMETRY_STATIC ",
    };
    const auto geometryTypeIndex = static_cast<int>(geometryType);
    if (geometryTypeIndex < ea::size(geometryDefines))
        vertexDefines_ += geometryDefines[geometryTypeIndex];
    else
        vertexDefines_ += Format("URHO3D_GEOMETRY_CUSTOM={} ", geometryTypeIndex);
}

void PipelineStateBuilder::ApplyVertexLayout(bool isShadowPass)
{
    const VertexLayoutFlags geometryVertexLayout = ScanVertexElements({ desc_.vertexElements_.data(), desc_.numVertexElements_ });
    const VertexLayoutFlags passVertexLayout = isShadowPass ? VertexLayoutFlag::ShadowMask : VertexLayoutFlag::DefaultMask;
    const VertexLayoutFlags vertexLayout = geometryVertexLayout & passVertexLayout;

    if (vertexLayout.Test(VertexLayoutFlag::HasColor))
        commonDefines_ += "URHO3D_VERTEX_HAS_COLOR ";

    if (vertexLayout.Test(VertexLayoutFlag::HasNormal))
        vertexDefines_ += "URHO3D_VERTEX_HAS_NORMAL ";
    if (vertexLayout.Test(VertexLayoutFlag::HasTangent))
        vertexDefines_ += "URHO3D_VERTEX_HAS_TANGENT ";
    if (vertexLayout.Test(VertexLayoutFlag::HasTexCoord0))
        vertexDefines_ += "URHO3D_VERTEX_HAS_TEXCOORD0 ";
    if (vertexLayout.Test(VertexLayoutFlag::HasTexCoord1))
        vertexDefines_ += "URHO3D_VERTEX_HAS_TEXCOORD1 ";
}

void PipelineStateBuilder::ApplyShadowPass(unsigned splitIndex, const LightProcessor* lightProcessor,
    const Material* material, const Pass* materialPass)
{
    const CookedLightParams& lightParams = lightProcessor->GetParams();
    const float biasMultiplier = lightParams.shadowDepthBiasMultiplier_[splitIndex];
    const BiasParameters& biasParameters = lightProcessor->GetLight()->GetShadowBias();

    if (shadowMapAllocator_->GetSettings().enableVarianceShadowMaps_)
    {
        desc_.colorWriteEnabled_ = true;
        desc_.constantDepthBias_ = 0.0f;
        desc_.slopeScaledDepthBias_ = 0.0f;
    }
    else
    {
        desc_.colorWriteEnabled_ = false;
        desc_.constantDepthBias_ = biasMultiplier * biasParameters.constantBias_;
        desc_.slopeScaledDepthBias_ = biasMultiplier * biasParameters.slopeScaledBias_;
    }

    desc_.depthWriteEnabled_ = materialPass->GetDepthWrite();
    desc_.depthCompareFunction_ = materialPass->GetDepthTestMode();

    desc_.cullMode_ = GetEffectiveCullMode(materialPass->GetCullMode(), material->GetShadowCullMode(), false);

    // TODO(renderer): Revisit this place
    // Perform further modification of depth bias on OpenGL ES, as shadow calculations' precision is limited
/*#ifdef GL_ES_VERSION_2_0
    const float multiplier = renderer_->GetMobileShadowBiasMul();
    const float addition = renderer_->GetMobileShadowBiasAdd();
    desc.constantDepthBias_ = desc.constantDepthBias_ * multiplier + addition;
    desc.slopeScaledDepthBias_ *= multiplier;
#endif*/

    commonDefines_ += "URHO3D_SHADOW_PASS ";
    if (shadowMapAllocator_->GetSettings().enableVarianceShadowMaps_)
        commonDefines_ += "URHO3D_VARIANCE_SHADOW_MAP VSM_SHADOW ";
    if (biasParameters.normalOffset_ > 0.0)
        vertexDefines_ += "URHO3D_SHADOW_NORMAL_OFFSET ";
}

void PipelineStateBuilder::ApplyLightVolumePass(const LightProcessor* lightProcessor)
{
    const Light* light = lightProcessor->GetLight();

    desc_.colorWriteEnabled_ = true;
    desc_.blendMode_ = light->IsNegative() ? BLEND_SUBTRACT : BLEND_ADD;

    /// TODO(renderer): Remove
    pixelDefines_ +=  "HWDEPTH ";
    if (cameraProcessor_->IsCameraOrthographic())
        commonDefines_ += "ORTHO ";

    if (light->GetLightType() != LIGHT_DIRECTIONAL)
    {
        if (lightProcessor->DoesOverlapCamera())
        {
            desc_.cullMode_ = GetEffectiveCullMode(CULL_CW, cameraProcessor_->IsCameraReversed());
            desc_.depthCompareFunction_ = CMP_GREATER;
        }
        else
        {
            desc_.cullMode_ = GetEffectiveCullMode(CULL_CCW, cameraProcessor_->IsCameraReversed());
            desc_.depthCompareFunction_ = CMP_LESSEQUAL;
        }
    }
    else
    {
        desc_.cullMode_ = CULL_NONE;
        desc_.depthCompareFunction_ = CMP_ALWAYS;
    }

    desc_.stencilTestEnabled_ = true;
    desc_.stencilCompareFunction_ = CMP_NOTEQUAL;
    desc_.stencilCompareMask_ = light->GetLightMaskEffective() & PORTABLE_LIGHTMASK;
    desc_.stencilReferenceValue_ = 0;
}

void PipelineStateBuilder::ApplyUserPass(const BatchCompositorPass* compositorPass, unsigned subpassIndex,
     const Material* material, const Pass* materialPass, const Drawable* drawable)
{
    const DrawableProcessorPassFlags passFlags = compositorPass->GetFlags();
    const bool isDeferred = subpassIndex == BatchCompositorPass::OverrideSubpass;
    if (subpassIndex == BatchCompositorPass::LightSubpass)
    {
        commonDefines_ += "URHO3D_ADDITIVE_LIGHT_PASS ";
    }
    else if (passFlags.Test(DrawableProcessorPassFlag::HasAmbientLighting))
    {
        commonDefines_ += "URHO3D_AMBIENT_PASS ";
        if (!isDeferred)
            commonDefines_ += Format("URHO3D_NUM_VERTEX_LIGHTS={} ", sceneProcessor_->GetSettings().maxVertexLights_);
    }

    static const ea::string ambientModeDefines[] = {
        "URHO3D_AMBIENT_CONSTANT ",
        "URHO3D_AMBIENT_FLAT ",
        "URHO3D_AMBIENT_DIRECTIONAL ",
    };

    vertexDefines_ += ambientModeDefines[static_cast<int>(sceneProcessor_->GetSettings().ambientMode_)];

    if (drawable->GetGlobalIlluminationType() == GlobalIlluminationType::UseLightMap)
        commonDefines_ += "URHO3D_HAS_LIGHTMAP LIGHTMAP ";

    if (Texture* diffuseTexture = material->GetTexture(TU_DIFFUSE))
    {
        pixelDefines_ += "URHO3D_MATERIAL_HAS_DIFFUSE ";
        // TODO(renderer): Throttle logging
        const int hint = GetTextureColorSpaceHint(diffuseTexture->GetLinear(), diffuseTexture->GetSRGB());
        if (hint > 1)
            URHO3D_LOGWARNING("Texture {} cannot be both sRGB and Linear", diffuseTexture->GetName());
        pixelDefines_ += Format("URHO3D_MATERIAL_DIFFUSE_HINT={} ", ea::min(1, hint));
    }
    if (material->GetTexture(TU_NORMAL))
        pixelDefines_ += "URHO3D_MATERIAL_HAS_NORMAL ";
    if (material->GetTexture(TU_SPECULAR))
        pixelDefines_ += "URHO3D_MATERIAL_HAS_SPECULAR ";
    if (material->GetTexture(TU_EMISSIVE))
        pixelDefines_ += "URHO3D_MATERIAL_HAS_EMISSIVE ";

    desc_.depthWriteEnabled_ = materialPass->GetDepthWrite();
    desc_.depthCompareFunction_ = materialPass->GetDepthTestMode();

    desc_.colorWriteEnabled_ = true;
    desc_.blendMode_ = materialPass->GetBlendMode();
    desc_.alphaToCoverageEnabled_ = materialPass->GetAlphaToCoverage();

    // TODO(renderer): Implement fill mode
    desc_.fillMode_ = FILL_SOLID;
    desc_.cullMode_ = GetEffectiveCullMode(materialPass->GetCullMode(),
        material->GetCullMode(), cameraProcessor_->IsCameraReversed());

    if (isDeferred && compositorPass->GetFlags().Test(DrawableProcessorPassFlag::DeferredLightMaskToStencil))
    {
        desc_.stencilTestEnabled_ = true;
        desc_.stencilOperationOnPassed_ = OP_REF;
        desc_.stencilWriteMask_ = PORTABLE_LIGHTMASK;
        desc_.stencilReferenceValue_ = drawable->GetLightMaskInZone() & PORTABLE_LIGHTMASK;
    }
}

void PipelineStateBuilder::ApplyPixelLight(const LightProcessor* lightProcessor, const Material* material)
{
    const Light* light = lightProcessor->GetLight();

    if (light->GetSpecularIntensity() > 0.0f && material->GetSpecular())
        pixelDefines_ += "URHO3D_HAS_SPECULAR_HIGHLIGHTS SPECULAR ";

    static const ea::string lightTypeDefines[] = {
        "URHO3D_LIGHT_DIRECTIONAL DIRLIGHT ",
        "URHO3D_LIGHT_SPOT SPOTLIGHT ",
        "URHO3D_LIGHT_POINT POINTLIGHT "
    };
    commonDefines_ += lightTypeDefines[static_cast<int>(light->GetLightType())];
    commonDefines_ += "PERPIXEL ";

    if (lightProcessor->HasShadow())
    {
        commonDefines_ += "URHO3D_HAS_SHADOW SHADOW ";
        if (shadowMapAllocator_->GetSettings().enableVarianceShadowMaps_)
            commonDefines_ += "URHO3D_VARIANCE_SHADOW_MAP VSM_SHADOW ";
        else
            commonDefines_ += Format("URHO3D_SHADOW_PCF_SIZE={} SIMPLE_SHADOW ", sceneProcessor_->GetSettings().pcfKernelSize_);
    }
}

void PipelineStateBuilder::FinalizeDescription(const Pass* materialPass)
{
    vertexDefines_ += commonDefines_;
    pixelDefines_ += commonDefines_;
    desc_.vertexShader_ = graphics_->GetShader(VS, "v2/" + materialPass->GetVertexShader(), vertexDefines_);
    desc_.pixelShader_ = graphics_->GetShader(PS, "v2/" + materialPass->GetPixelShader(), pixelDefines_);
}

}
