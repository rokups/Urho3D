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
#include "../Graphics/GraphicsImpl.h"
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

}

ShaderProgramCompositor::ShaderProgramCompositor(Context* context)
    : Object(context)
{
    auto graphics = GetSubsystem<Graphics>();
    constantBuffersSupported_ = graphics->GetCaps().constantBuffersSupported_;
}

void ShaderProgramCompositor::SetSettings(const ShaderProgramCompositorSettings& settings)
{
    settings_ = settings;
}

void ShaderProgramCompositor::SetFrameSettings(const CameraProcessor* cameraProcessor)
{
    isCameraOrthographic_ = cameraProcessor->IsCameraOrthographic();
    isCameraClipped_ = cameraProcessor->IsCameraClipped();
    isCameraReversed_ = cameraProcessor->IsCameraReversed();
}

void ShaderProgramCompositor::ProcessUserBatch(ShaderProgramDesc& result, DrawableProcessorPassFlags flags,
    Drawable* drawable, Geometry* geometry, GeometryType geometryType, Material* material, Pass* pass,
    Light* light, bool hasShadow, BatchCompositorSubpass subpass)
{
    SetupShaders(result, pass);
    ApplyCommonDefines(result, flags, pass);
    ApplyGeometryVertexDefines(result, flags, geometry, geometryType);

    ApplyLayoutVertexAndCommonDefinesForUserPass(result, geometry->GetVertexBuffer(0));
    ApplyMaterialPixelDefinesForUserPass(result, material);

    if (isCameraClipped_)
        result.vertexShaderDefines_ += "URHO3D_CLIP_PLANE ";

    const bool isDeferred = subpass == BatchCompositorSubpass::Deferred;
    const bool isDepthOnly = flags.Test(DrawableProcessorPassFlag::DepthOnlyPass);
    if (subpass == BatchCompositorSubpass::Light)
        result.commonShaderDefines_ += "URHO3D_ADDITIVE_LIGHT_PASS ";
    else if (flags.Test(DrawableProcessorPassFlag::HasAmbientLighting))
        ApplyAmbientLightingVertexAndCommonDefinesForUserPass(result, drawable, isDeferred);

    if (isDeferred)
        result.commonShaderDefines_ += "URHO3D_NUM_RENDER_TARGETS=4 ";
    else if (isDepthOnly)
        result.commonShaderDefines_ += "URHO3D_NUM_RENDER_TARGETS=0 ";

    if (light)
        ApplyPixelLightPixelAndCommonDefines(result, light, hasShadow, material->GetSpecular());
}

void ShaderProgramCompositor::ProcessShadowBatch(ShaderProgramDesc& result,
    Geometry* geometry, GeometryType geometryType, Material* material, Pass* pass, Light* light)
{
    const DrawableProcessorPassFlags flags = DrawableProcessorPassFlag::DepthOnlyPass;
    SetupShaders(result, pass);
    ApplyCommonDefines(result, flags, pass);
    ApplyGeometryVertexDefines(result, flags, geometry, geometryType);
    ApplyDefinesForShadowPass(result, light, geometry->GetVertexBuffer(0), material, pass);
}

void ShaderProgramCompositor::ProcessLightVolumeBatch(ShaderProgramDesc& result,
    Geometry* geometry, GeometryType geometryType, Pass* pass, Light* light, bool hasShadow)
{
    const DrawableProcessorPassFlags flags = DrawableProcessorPassFlag::DisableInstancing;
    SetupShaders(result, pass);
    ApplyCommonDefines(result, flags, pass);
    ApplyGeometryVertexDefines(result, flags, geometry, geometryType);
    ApplyPixelLightPixelAndCommonDefines(result, light, hasShadow, true);
    ApplyDefinesForLightVolumePass(result);
}

void ShaderProgramCompositor::SetupShaders(ShaderProgramDesc& result, Pass* pass)
{
    result.vertexShaderName_ = "v2/" + pass->GetVertexShader();
    result.pixelShaderName_ = "v2/" + pass->GetPixelShader();
}

void ShaderProgramCompositor::ApplyCommonDefines(ShaderProgramDesc& result,
    DrawableProcessorPassFlags flags, Pass* pass) const
{
    if (constantBuffersSupported_)
        result.commonShaderDefines_ += "URHO3D_USE_CBUFFERS ";

    if (isCameraReversed_)
        result.commonShaderDefines_ += "URHO3D_CAMERA_REVERSED ";

    if (!flags.Test(DrawableProcessorPassFlag::DepthOnlyPass))
    {
        if (settings_.sceneProcessor_.linearSpaceLighting_)
            result.commonShaderDefines_ += "URHO3D_GAMMA_CORRECTION ";

        switch (settings_.sceneProcessor_.specularQuality_)
        {
        case SpecularQuality::Simple:
            result.commonShaderDefines_ += "URHO3D_SPECULAR=1 ";
            break;
        case SpecularQuality::Antialiased:
            result.commonShaderDefines_ += "URHO3D_SPECULAR=2 ";
            break;
        default:
            break;
        }

        if (settings_.sceneProcessor_.reflectionQuality_ == ReflectionQuality::Vertex)
            result.commonShaderDefines_ += "URHO3D_VERTEX_REFLECTION ";
    }

    if (flags.Test(DrawableProcessorPassFlag::NeedReadableDepth) && settings_.renderBufferManager_.readableDepth_)
        result.commonShaderDefines_ += "URHO3D_HAS_READABLE_DEPTH ";

    result.vertexShaderDefines_ += pass->GetEffectiveVertexShaderDefines();
    result.vertexShaderDefines_ += " ";
    result.pixelShaderDefines_ += pass->GetEffectivePixelShaderDefines();
    result.pixelShaderDefines_ += " ";
}

void ShaderProgramCompositor::ApplyGeometryVertexDefines(ShaderProgramDesc& result,
    DrawableProcessorPassFlags flags, Geometry* geometry, GeometryType geometryType) const
{
    result.isInstancingUsed_ = IsInstancingUsed(flags, geometry, geometryType);
    if (result.isInstancingUsed_)
        result.vertexShaderDefines_ += "URHO3D_INSTANCING ";

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
        result.vertexShaderDefines_ += geometryDefines[geometryTypeIndex];
    else
        result.vertexShaderDefines_ += Format("URHO3D_GEOMETRY_CUSTOM={} ", geometryTypeIndex);
}

void ShaderProgramCompositor::ApplyPixelLightPixelAndCommonDefines(ShaderProgramDesc& result,
    Light* light, bool hasShadow, bool materialHasSpecular) const
{
    if (light->GetShapeTexture())
        result.commonShaderDefines_ += "URHO3D_LIGHT_CUSTOM_SHAPE ";

    if (light->GetRampTexture())
        result.pixelShaderDefines_ += "URHO3D_LIGHT_CUSTOM_RAMP ";

    static const ea::string lightTypeDefines[] = {
        "URHO3D_LIGHT_DIRECTIONAL ",
        "URHO3D_LIGHT_SPOT ",
        "URHO3D_LIGHT_POINT "
    };
    result.commonShaderDefines_ += lightTypeDefines[static_cast<int>(light->GetLightType())];

    if (hasShadow)
    {
        const unsigned maxCascades = light->GetLightType() == LIGHT_DIRECTIONAL ? MAX_CASCADE_SPLITS : 1u;

        result.commonShaderDefines_ += "URHO3D_HAS_SHADOW ";
        if (maxCascades > 1)
            result.commonShaderDefines_ += Format("URHO3D_MAX_SHADOW_CASCADES={} ", maxCascades);
        if (settings_.shadowMapAllocator_.enableVarianceShadowMaps_)
            result.commonShaderDefines_ += "URHO3D_VARIANCE_SHADOW_MAP ";
        else
            result.commonShaderDefines_ += Format("URHO3D_SHADOW_PCF_SIZE={} ",
                settings_.sceneProcessor_.pcfKernelSize_);
    }
}

void ShaderProgramCompositor::ApplyLayoutVertexAndCommonDefinesForUserPass(
    ShaderProgramDesc& result, VertexBuffer* vertexBuffer) const
{
    if (vertexBuffer->HasElement(SEM_NORMAL))
        result.vertexShaderDefines_ += "URHO3D_VERTEX_HAS_NORMAL ";
    if (vertexBuffer->HasElement(SEM_TANGENT))
        result.vertexShaderDefines_ += "URHO3D_VERTEX_HAS_TANGENT ";
    if (vertexBuffer->HasElement(SEM_TEXCOORD, 0))
        result.vertexShaderDefines_ += "URHO3D_VERTEX_HAS_TEXCOORD0 ";
    if (vertexBuffer->HasElement(SEM_TEXCOORD, 1))
        result.vertexShaderDefines_ += "URHO3D_VERTEX_HAS_TEXCOORD1 ";

    if (vertexBuffer->HasElement(SEM_COLOR))
        result.commonShaderDefines_ += "URHO3D_VERTEX_HAS_COLOR ";
}

void ShaderProgramCompositor::ApplyMaterialPixelDefinesForUserPass(ShaderProgramDesc& result, Material* material) const
{
    if (Texture* diffuseTexture = material->GetTexture(TU_DIFFUSE))
    {
        result.pixelShaderDefines_ += "URHO3D_MATERIAL_HAS_DIFFUSE ";
        const int hint = GetTextureColorSpaceHint(diffuseTexture->GetLinear(), diffuseTexture->GetSRGB());
        if (hint > 1)
            URHO3D_LOGWARNING("Texture {} cannot be both sRGB and Linear", diffuseTexture->GetName());
        result.pixelShaderDefines_ += Format("URHO3D_MATERIAL_DIFFUSE_HINT={} ", ea::min(1, hint));
    }

    if (material->GetTexture(TU_NORMAL))
        result.pixelShaderDefines_ += "URHO3D_MATERIAL_HAS_NORMAL ";

    if (material->GetTexture(TU_SPECULAR))
        result.pixelShaderDefines_ += "URHO3D_MATERIAL_HAS_SPECULAR ";

    if (Texture* envTexture = material->GetTexture(TU_ENVIRONMENT))
    {
        if (envTexture->IsInstanceOf<Texture2D>())
            result.commonShaderDefines_ += "URHO3D_MATERIAL_HAS_PLANAR_ENVIRONMENT ";
    }

    if (Texture* emissiveTexture = material->GetTexture(TU_EMISSIVE))
    {
        result.pixelShaderDefines_ += "URHO3D_MATERIAL_HAS_EMISSIVE ";
        const int hint = GetTextureColorSpaceHint(emissiveTexture->GetLinear(), emissiveTexture->GetSRGB());
        if (hint > 1)
            URHO3D_LOGWARNING("Texture {} cannot be both sRGB and Linear", emissiveTexture->GetName());
        result.pixelShaderDefines_ += Format("URHO3D_MATERIAL_EMISSIVE_HINT={} ", ea::min(1, hint));
    }
}

void ShaderProgramCompositor::ApplyAmbientLightingVertexAndCommonDefinesForUserPass(ShaderProgramDesc& result,
    Drawable* drawable, bool isGeometryBufferPass) const
{
    result.commonShaderDefines_ += "URHO3D_AMBIENT_PASS ";
    if (isGeometryBufferPass)
        result.commonShaderDefines_ += "URHO3D_GBUFFER_PASS ";
    else if (settings_.sceneProcessor_.maxVertexLights_ > 0)
        result.commonShaderDefines_ += Format("URHO3D_NUM_VERTEX_LIGHTS={} ", settings_.sceneProcessor_.maxVertexLights_);

    if (drawable->GetGlobalIlluminationType() == GlobalIlluminationType::UseLightMap)
        result.commonShaderDefines_ += "URHO3D_HAS_LIGHTMAP ";

    static const ea::string ambientModeDefines[] = {
        "URHO3D_AMBIENT_CONSTANT ",
        "URHO3D_AMBIENT_FLAT ",
        "URHO3D_AMBIENT_DIRECTIONAL ",
    };

    result.vertexShaderDefines_ += ambientModeDefines[static_cast<int>(settings_.sceneProcessor_.ambientMode_)];
}

void ShaderProgramCompositor::ApplyDefinesForShadowPass(ShaderProgramDesc& result,
    Light* light, VertexBuffer* vertexBuffer, Material* material, Pass* pass) const
{
    if (vertexBuffer->HasElement(SEM_NORMAL))
        result.vertexShaderDefines_ += "URHO3D_VERTEX_HAS_NORMAL ";

    if (light->GetShadowBias().normalOffset_ > 0.0)
        result.vertexShaderDefines_ += "URHO3D_SHADOW_NORMAL_OFFSET ";

    if (pass->IsAlphaMask())
    {
        if (vertexBuffer->HasElement(SEM_TEXCOORD, 0))
            result.vertexShaderDefines_ += "URHO3D_VERTEX_HAS_TEXCOORD0 ";
        if (Texture* diffuseTexture = material->GetTexture(TU_DIFFUSE))
            result.pixelShaderDefines_ += "URHO3D_MATERIAL_HAS_DIFFUSE ";
    }

    result.commonShaderDefines_ += "URHO3D_SHADOW_PASS ";
    if (settings_.shadowMapAllocator_.enableVarianceShadowMaps_)
        result.commonShaderDefines_ += "URHO3D_VARIANCE_SHADOW_MAP ";
    else
        result.commonShaderDefines_ += "URHO3D_NUM_RENDER_TARGETS=0 ";
}

void ShaderProgramCompositor::ApplyDefinesForLightVolumePass(ShaderProgramDesc& result) const
{
    result.commonShaderDefines_ += "URHO3D_LIGHT_VOLUME_PASS ";
    if (isCameraOrthographic_)
        result.commonShaderDefines_ += "URHO3D_ORTHOGRAPHIC_DEPTH ";
    if (settings_.sceneProcessor_.lightingMode_ == DirectLightingMode::DeferredPBR)
        result.commonShaderDefines_ += "URHO3D_PHYSICAL_MATERIAL ";
}

bool ShaderProgramCompositor::IsInstancingUsed(
    DrawableProcessorPassFlags flags, Geometry* geometry, GeometryType geometryType) const
{
    return !flags.Test(DrawableProcessorPassFlag::DisableInstancing)
        && settings_.instancingBuffer_.enableInstancing_
        && geometry->IsInstanced(geometryType);
}

}
