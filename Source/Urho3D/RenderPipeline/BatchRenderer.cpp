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
#include "../Graphics/Camera.h"
#include "../Graphics/DrawCommandQueue.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Octree.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/Zone.h"
#include "../IO/Log.h"
#include "../RenderPipeline/BatchRenderer.h"
#include "../RenderPipeline/DrawableProcessor.h"
#include "../RenderPipeline/InstancingBuffer.h"
#include "../RenderPipeline/LightProcessor.h"
#include "../RenderPipeline/PipelineBatchSortKey.h"
#include "../Scene/Scene.h"

#include <EASTL/sort.h>

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

/// Return shader parameter for camera depth mode.
Vector4 GetCameraDepthModeParameter(const Camera& camera)
{
    Vector4 depthMode = Vector4::ZERO;
    if (camera.IsOrthographic())
    {
        depthMode.x_ = 1.0f;
#ifdef URHO3D_OPENGL
        depthMode.z_ = 0.5f;
        depthMode.w_ = 0.5f;
#else
        depthMode.z_ = 1.0f;
#endif
    }
    else
        depthMode.w_ = 1.0f / camera.GetFarClip();
    return depthMode;
}

/// Return shader parameter for camera depth reconstruction.
Vector4 GetCameraDepthReconstructParameter(const Camera& camera)
{
    const float nearClip = camera.GetNearClip();
    const float farClip = camera.GetFarClip();
    return {
        farClip / (farClip - nearClip),
        -nearClip / (farClip - nearClip),
        camera.IsOrthographic() ? 1.0f : 0.0f,
        camera.IsOrthographic() ? 0.0f : 1.0f
    };
}

/// Return shader parameter for zone fog.
Vector4 GetFogParameter(const Camera& camera)
{
    const float farClip = camera.GetFarClip();
    float fogStart = Min(camera.GetEffectiveFogStart(), farClip);
    float fogEnd = Min(camera.GetEffectiveFogEnd(), farClip);
    if (fogStart >= fogEnd * (1.0f - M_LARGE_EPSILON))
        fogStart = fogEnd * (1.0f - M_LARGE_EPSILON);
    const float fogRange = Max(fogEnd - fogStart, M_EPSILON);
    return {
        fogEnd / farClip,
        farClip / fogRange,
        0.0f,
        0.0f
    };
}

/// Batch renderer to command queue.
// TODO(renderer): Add template parameter to avoid branching?
class DrawCommandCompositor : public NonCopyable, private BatchRenderingContext
{
public:
    DrawCommandCompositor(const BatchRenderingContext& ctx, const BatchRendererSettings& settings,
        const DrawableProcessor& drawableProcessor, InstancingBuffer& instancingBuffer, BatchRenderFlags flags)
        : BatchRenderingContext(ctx)
        , settings_(settings)
        , drawableProcessor_(drawableProcessor)
        , instancingBuffer_(instancingBuffer)
        , frameInfo_(drawableProcessor_.GetFrameInfo())
        , scene_(*frameInfo_.scene_)
        , lights_(drawableProcessor_.GetLightProcessors())
        , cameraNode_(*camera_.GetNode())
        , enabled_(flags, instancingBuffer)
    {
    }

    /// Process batches
    /// @{
    void ProcessSceneBatch(const PipelineBatch& pipelineBatch)
    {
        ProcessBatch(pipelineBatch, pipelineBatch.GetSourceBatch());
    }

    void ProcessLightVolumeBatch(const PipelineBatch& pipelineBatch)
    {
        Light* light = lights_[pipelineBatch.pixelLightIndex_]->GetLight();
        lightVolumeHelpers_.transform_ = light->GetVolumeTransform(&camera_);
        lightVolumeHelpers_.sourceBatch_.worldTransform_ = &lightVolumeHelpers_.transform_;
        ProcessBatch(pipelineBatch, lightVolumeHelpers_.sourceBatch_);
    }

    void FlushDrawCommands()
    {
        if (instancingGroup_.count_ > 0)
            DrawObjectInstanced();
    }
    /// @}

private:
    /// Extract and process batch state w/o any changes in DrawQueue
    /// @{
    void CheckDirtyCommonState(const PipelineBatch& pipelineBatch)
    {
        dirty_.pipelineState_ = current_.pipelineState_ != pipelineBatch.pipelineState_;
        current_.pipelineState_ = pipelineBatch.pipelineState_;

        const float constantDepthBias = current_.pipelineState_->GetDesc().constantDepthBias_;
        if (current_.constantDepthBias_ != constantDepthBias)
        {
            current_.constantDepthBias_ = constantDepthBias;
            dirty_.cameraConstants_ = true;
        }

        dirty_.material_ = current_.material_ != pipelineBatch.material_;
        current_.material_ = pipelineBatch.material_;

        dirty_.geometry_ = current_.geometry_ != pipelineBatch.geometry_;
        current_.geometry_ = pipelineBatch.geometry_;
    }

    void CheckDirtyPixelLight(const PipelineBatch& pipelineBatch)
    {
        dirty_.pixelLightConstants_ = current_.pixelLightIndex_ != pipelineBatch.pixelLightIndex_;
        if (!dirty_.pixelLightConstants_)
            return;

        current_.pixelLightIndex_ = pipelineBatch.pixelLightIndex_;
        current_.pixelLightEnabled_ = current_.pixelLightIndex_ != M_MAX_UNSIGNED;
        if (current_.pixelLightEnabled_)
        {
            current_.pixelLightParams_ = &lights_[current_.pixelLightIndex_]->GetParams();
            dirty_.pixelLightTextures_ = current_.pixelLightRamp_ != current_.pixelLightParams_->lightRamp_
                || current_.pixelLightShape_ != current_.pixelLightParams_->lightShape_
                || current_.pixelLightShadowMap_ != current_.pixelLightParams_->shadowMap_;
            if (dirty_.pixelLightTextures_)
            {
                current_.pixelLightRamp_ = current_.pixelLightParams_->lightRamp_;
                current_.pixelLightShape_ = current_.pixelLightParams_->lightShape_;
                current_.pixelLightShadowMap_ = current_.pixelLightParams_->shadowMap_;
            }
        }
    }

    void CheckDirtyVertexLight(const LightAccumulator& lightAccumulator)
    {
        const auto previousVertexLights = current_.vertexLights_;
        current_.vertexLights_ = lightAccumulator.GetVertexLights();
        dirty_.vertexLightConstants_ = previousVertexLights != current_.vertexLights_;
        if (!dirty_.vertexLightConstants_)
            return;

        static const CookedLightParams nullVertexLight{};
        for (unsigned i = 0; i < MAX_VERTEX_LIGHTS; ++i)
        {
            const CookedLightParams& params = current_.vertexLights_[i] != M_MAX_UNSIGNED
                ? lights_[current_.vertexLights_[i]]->GetParams() : nullVertexLight;
            const Vector3& color = params.GetColor(settings_.gammaCorrection_);

            current_.vertexLightsData_[i * 3] = { color, params.inverseRange_ };
            current_.vertexLightsData_[i * 3 + 1] = { params.direction_, params.spotCutoff_ };
            current_.vertexLightsData_[i * 3 + 2] = { params.position_, params.inverseSpotCutoff_ };
        }
    }

    void CheckDirtyLightmap(const SourceBatch& sourceBatch)
    {
        dirty_.lightmapConstants_ = current_.lightmapScaleOffset_ != sourceBatch.lightmapScaleOffset_;
        if (!dirty_.lightmapConstants_)
            return;

        current_.lightmapScaleOffset_ = sourceBatch.lightmapScaleOffset_;

        Texture2D* lightmapTexture = current_.lightmapScaleOffset_
            ? scene_.GetLightmapTexture(sourceBatch.lightmapIndex_)
            : nullptr;

        dirty_.lightmapTextures_ = current_.lightmapTexture_ != lightmapTexture;
        current_.lightmapTexture_ = lightmapTexture;
    }

    void ExtractObjectConstants(const SourceBatch& sourceBatch, const LightAccumulator* lightAccumulator)
    {
        if (enabled_.ambientLighting_)
        {
            if (settings_.ambientMode_ == DrawableAmbientMode::Flat)
            {
                const Vector3 ambient = lightAccumulator->sphericalHarmonics_.EvaluateAverage();
                if (settings_.gammaCorrection_)
                    object_.ambient_ = Vector4(ambient, 1.0f);
                else
                    object_.ambient_ = Color(ambient).LinearToGamma().ToVector4();
            }
            else if (settings_.ambientMode_ == DrawableAmbientMode::Directional)
            {
                object_.sh_ = &lightAccumulator->sphericalHarmonics_;
            }
        }

        object_.geometryType_ = sourceBatch.geometryType_;
        object_.worldTransform_ = sourceBatch.worldTransform_;
        object_.numWorldTransforms_ = sourceBatch.numWorldTransforms_;
    }
    /// @}

    /// Commit changes to DrawQueue
    /// @{
    void UpdateDirtyConstants()
    {
        if (dirty_.pipelineState_)
        {
            drawQueue_.SetPipelineState(current_.pipelineState_);
            dirty_.pipelineState_ = false;
        }

        if (drawQueue_.BeginShaderParameterGroup(SP_FRAME, dirty_.frameConstants_))
        {
            AddFrameConstants();
            drawQueue_.CommitShaderParameterGroup(SP_FRAME);
            dirty_.frameConstants_ = false;
        }

        if (drawQueue_.BeginShaderParameterGroup(SP_CAMERA, dirty_.cameraConstants_))
        {
            AddCameraConstants(current_.constantDepthBias_);
            drawQueue_.CommitShaderParameterGroup(SP_CAMERA);
            dirty_.cameraConstants_ = false;
        }

        // Commit pixel light constants once during shadow map rendering to support normal bias
        if (outputShadowSplit_ != nullptr)
        {
            if (drawQueue_.BeginShaderParameterGroup(SP_LIGHT, false))
            {
                const CookedLightParams& params = outputShadowSplit_->GetLightProcessor()->GetParams();
                AddPixelLightConstants(params);
                drawQueue_.CommitShaderParameterGroup(SP_LIGHT);
            }
        }
        else if (enabled_.anyLighting_)
        {
            const bool lightConstantsDirty = dirty_.pixelLightConstants_ || dirty_.vertexLightConstants_;
            if (drawQueue_.BeginShaderParameterGroup(SP_LIGHT, lightConstantsDirty))
            {
                if (enabled_.vertexLighting_)
                    AddVertexLightConstants();
                if (current_.pixelLightEnabled_)
                    AddPixelLightConstants(*current_.pixelLightParams_);
                drawQueue_.CommitShaderParameterGroup(SP_LIGHT);
            }
        }

        if (drawQueue_.BeginShaderParameterGroup(SP_MATERIAL, dirty_.material_ || dirty_.lightmapConstants_))
        {
            const auto& materialParameters = current_.material_->GetShaderParameters();
            for (const auto& parameter : materialParameters)
                drawQueue_.AddShaderParameter(parameter.first, parameter.second.value_);

            if (enabled_.ambientLighting_ && current_.lightmapScaleOffset_)
                drawQueue_.AddShaderParameter(VSP_LMOFFSET, *current_.lightmapScaleOffset_);

            drawQueue_.CommitShaderParameterGroup(SP_MATERIAL);
        }
    }

    void UpdateDirtyResources()
    {
        const bool resourcesDirty = dirty_.material_ || dirty_.lightmapTextures_ || dirty_.pixelLightTextures_;
        if (resourcesDirty)
        {
            for (const ShaderResourceDesc& desc : globalResources_)
                drawQueue_.AddShaderResource(desc.unit_, desc.texture_);

            const auto& materialTextures = current_.material_->GetTextures();
            for (const auto& texture : materialTextures)
            {
                if (!current_.lightmapTexture_ || texture.first != TU_EMISSIVE)
                    drawQueue_.AddShaderResource(texture.first, texture.second);
            }

            if (current_.lightmapTexture_)
                drawQueue_.AddShaderResource(TU_EMISSIVE, current_.lightmapTexture_);
            if (current_.pixelLightRamp_)
                drawQueue_.AddShaderResource(TU_LIGHTRAMP, current_.pixelLightRamp_);
            if (current_.pixelLightShape_)
                drawQueue_.AddShaderResource(TU_LIGHTSHAPE, current_.pixelLightShape_);
            if (current_.pixelLightShadowMap_)
                drawQueue_.AddShaderResource(TU_SHADOWMAP, current_.pixelLightShadowMap_);

            drawQueue_.CommitShaderResources();
        }
    }

    void AddFrameConstants()
    {
        for (const ShaderParameterDesc& shaderParameter : frameParameters_)
            drawQueue_.AddShaderParameter(shaderParameter.name_, shaderParameter.value_);

        drawQueue_.AddShaderParameter(VSP_DELTATIME, frameInfo_.timeStep_);
        drawQueue_.AddShaderParameter(VSP_ELAPSEDTIME, scene_.GetElapsedTime());
    }

    void AddCameraConstants(float constantDepthBias)
    {
        for (const ShaderParameterDesc& shaderParameter : cameraParameters_)
            drawQueue_.AddShaderParameter(shaderParameter.name_, shaderParameter.value_);

        const Matrix3x4 cameraEffectiveTransform = camera_.GetEffectiveWorldTransform();
        drawQueue_.AddShaderParameter(VSP_CAMERAPOS, cameraEffectiveTransform.Translation());
        drawQueue_.AddShaderParameter(VSP_VIEWINV, cameraEffectiveTransform);
        drawQueue_.AddShaderParameter(VSP_VIEW, camera_.GetView());

        const float nearClip = camera_.GetNearClip();
        const float farClip = camera_.GetFarClip();
        drawQueue_.AddShaderParameter(VSP_NEARCLIP, nearClip);
        drawQueue_.AddShaderParameter(VSP_FARCLIP, farClip);

        if (outputShadowSplit_)
        {
            const CookedLightParams& lightParams = outputShadowSplit_->GetLightProcessor()->GetParams();
            drawQueue_.AddShaderParameter(VSP_NORMALOFFSETSCALE,
                lightParams.shadowNormalBias_[outputShadowSplit_->GetSplitIndex()]);
        }

        drawQueue_.AddShaderParameter(VSP_DEPTHMODE, GetCameraDepthModeParameter(camera_));
        drawQueue_.AddShaderParameter(PSP_DEPTHRECONSTRUCT, GetCameraDepthReconstructParameter(camera_));

        Vector3 nearVector, farVector;
        camera_.GetFrustumSize(nearVector, farVector);
        drawQueue_.AddShaderParameter(VSP_FRUSTUMSIZE, farVector);

        drawQueue_.AddShaderParameter(VSP_VIEWPROJ, camera_.GetEffectiveGPUViewProjection(constantDepthBias));

        const Color ambientColorGamma = camera_.GetEffectiveAmbientColor() * camera_.GetEffectiveAmbientBrightness();
        drawQueue_.AddShaderParameter(PSP_AMBIENTCOLOR,
            settings_.gammaCorrection_ ? ambientColorGamma.GammaToLinear() : ambientColorGamma);
        drawQueue_.AddShaderParameter(PSP_FOGCOLOR, camera_.GetEffectiveFogColor());
        drawQueue_.AddShaderParameter(PSP_FOGPARAMS, GetFogParameter(camera_));
    }

    void AddVertexLightConstants()
    {
        drawQueue_.AddShaderParameter(VSP_VERTEXLIGHTS, ea::span<const Vector4>{ current_.vertexLightsData_ });
    }

    void AddPixelLightConstants(const CookedLightParams& params)
    {
        drawQueue_.AddShaderParameter(VSP_LIGHTDIR, params.direction_);
        drawQueue_.AddShaderParameter(VSP_LIGHTPOS,
            Vector4{ params.position_, params.inverseRange_ });
        drawQueue_.AddShaderParameter(PSP_LIGHTCOLOR,
            Vector4{ params.GetColor(settings_.gammaCorrection_), params.effectiveSpecularIntensity_ });

        drawQueue_.AddShaderParameter(PSP_LIGHTRAD, params.volumetricRadius_);
        drawQueue_.AddShaderParameter(PSP_LIGHTLENGTH, params.volumetricLength_);

        // TODO(renderer): Cleanup constants
        drawQueue_.AddShaderParameter("SpotAngle", Vector2{ params.spotCutoff_, params.inverseSpotCutoff_ });
        if (params.lightShape_)
            drawQueue_.AddShaderParameter("LightShapeMatrix", params.lightShapeMatrix_);

        if (params.numLightMatrices_ > 0)
        {
            ea::span<const Matrix4> shadowMatricesSpan{ params.lightMatrices_.data(), params.numLightMatrices_ };
            drawQueue_.AddShaderParameter(VSP_LIGHTMATRICES, shadowMatricesSpan);
        }

        if (params.shadowMap_)
        {
            drawQueue_.AddShaderParameter(PSP_SHADOWDEPTHFADE, params.shadowDepthFade_);
            drawQueue_.AddShaderParameter(PSP_SHADOWINTENSITY, params.shadowIntensity_);
            drawQueue_.AddShaderParameter(PSP_SHADOWMAPINVSIZE, params.shadowMapInvSize_);
            drawQueue_.AddShaderParameter(PSP_SHADOWSPLITS, params.shadowSplitDistances_);
            drawQueue_.AddShaderParameter(PSP_SHADOWCUBEUVBIAS, params.shadowCubeUVBias_);
            drawQueue_.AddShaderParameter(PSP_SHADOWCUBEADJUST, params.shadowCubeAdjust_);
            drawQueue_.AddShaderParameter(PSP_VSMSHADOWPARAMS, settings_.varianceShadowMapParams_);
        }
    }

    void AddObjectConstants()
    {
        if (enabled_.ambientLighting_)
        {
            if (settings_.ambientMode_ == DrawableAmbientMode::Flat)
                drawQueue_.AddShaderParameter(VSP_AMBIENT, object_.ambient_);
            else if (settings_.ambientMode_ == DrawableAmbientMode::Directional)
            {
                const SphericalHarmonicsDot9& sh = *object_.sh_;
                drawQueue_.AddShaderParameter(VSP_SHAR, sh.Ar_);
                drawQueue_.AddShaderParameter(VSP_SHAG, sh.Ag_);
                drawQueue_.AddShaderParameter(VSP_SHAB, sh.Ab_);
                drawQueue_.AddShaderParameter(VSP_SHBR, sh.Br_);
                drawQueue_.AddShaderParameter(VSP_SHBG, sh.Bg_);
                drawQueue_.AddShaderParameter(VSP_SHBB, sh.Bb_);
                drawQueue_.AddShaderParameter(VSP_SHC, sh.C_);
            }
        }

        switch (object_.geometryType_)
        {
        case GEOM_SKINNED:
            drawQueue_.AddShaderParameter(VSP_SKINMATRICES,
                ea::span<const Matrix3x4>(object_.worldTransform_, object_.numWorldTransforms_));
            break;

        case GEOM_BILLBOARD:
            drawQueue_.AddShaderParameter(VSP_MODEL, *object_.worldTransform_);
            if (object_.numWorldTransforms_ > 1)
                drawQueue_.AddShaderParameter(VSP_BILLBOARDROT, object_.worldTransform_[1].RotationMatrix());
            else
                drawQueue_.AddShaderParameter(VSP_BILLBOARDROT, cameraNode_.GetWorldRotation().RotationMatrix());
            break;

        default:
            drawQueue_.AddShaderParameter(VSP_MODEL, *object_.worldTransform_);
            break;
        }
    }

    void AddObjectInstanceData()
    {
        instancingBuffer_.SetElements(object_.worldTransform_, 0, 3);
        if (enabled_.ambientLighting_)
        {
            if (settings_.ambientMode_ == DrawableAmbientMode::Flat)
                instancingBuffer_.SetElements(&object_.ambient_, 3, 1);
            else if (settings_.ambientMode_ == DrawableAmbientMode::Directional)
                instancingBuffer_.SetElements(object_.sh_, 3, 7);
        }
    }
    /// @}

    /// Draw ops
    /// @{
    void DrawObject()
    {
        IndexBuffer* indexBuffer = current_.geometry_->GetIndexBuffer();
        if (dirty_.geometry_)
        {
            drawQueue_.SetBuffers({ current_.geometry_->GetVertexBuffers(), indexBuffer, nullptr });
            dirty_.geometry_ = false;
        }

        if (indexBuffer != nullptr)
            drawQueue_.DrawIndexed(current_.geometry_->GetIndexStart(), current_.geometry_->GetIndexCount());
        else
            drawQueue_.Draw(current_.geometry_->GetVertexStart(), current_.geometry_->GetVertexCount());
    }

    void DrawObjectInstanced()
    {
        assert(instancingGroup_.count_ > 0);
        Geometry* geometry = instancingGroup_.geometry_;
        drawQueue_.SetBuffers({ geometry->GetVertexBuffers(), geometry->GetIndexBuffer(),
            instancingBuffer_.GetVertexBuffer() });
        drawQueue_.DrawIndexedInstanced(geometry->GetIndexStart(), geometry->GetIndexCount(),
            instancingGroup_.start_, instancingGroup_.count_);
        instancingGroup_.count_ = 0;
    }
    /// @}

    void ProcessBatch(const PipelineBatch& pipelineBatch, const SourceBatch& sourceBatch)
    {
        const LightAccumulator* lightAccumulator = enabled_.ambientLighting_ || enabled_.vertexLighting_
            ? &drawableProcessor_.GetGeometryLighting(pipelineBatch.drawableIndex_)
            : nullptr;

        ExtractObjectConstants(sourceBatch, lightAccumulator);

        // Update dirty flags and cached state
        CheckDirtyCommonState(pipelineBatch);
        if (enabled_.pixelLighting_)
            CheckDirtyPixelLight(pipelineBatch);
        if (enabled_.vertexLighting_)
            CheckDirtyVertexLight(*lightAccumulator);
        if (enabled_.ambientLighting_)
            CheckDirtyLightmap(sourceBatch);

        const bool resetInstancingGroup = instancingGroup_.count_ == 0 || dirty_.IsAnyStateDirty();
        if (resetInstancingGroup)
        {
            if (instancingGroup_.count_ > 0)
                DrawObjectInstanced();

            UpdateDirtyConstants();
            UpdateDirtyResources();

            const bool beginInstancingGroup = enabled_.staticInstancing_
                && pipelineBatch.geometryType_ == GEOM_STATIC
                && pipelineBatch.geometry_->GetIndexBuffer() != nullptr;
            if (beginInstancingGroup)
            {
                instancingGroup_.count_ = 1;
                instancingGroup_.start_ = instancingBuffer_.AddInstance();
                instancingGroup_.geometry_ = current_.geometry_;
                AddObjectInstanceData();
            }
            else
            {
                drawQueue_.BeginShaderParameterGroup(SP_OBJECT, true);
                AddObjectConstants();
                drawQueue_.CommitShaderParameterGroup(SP_OBJECT);

                DrawObject();
            }
        }
        else
        {
            ++instancingGroup_.count_;
            instancingBuffer_.AddInstance();
            AddObjectInstanceData();
        }
    }

    /// External state (required)
    /// @{
    const BatchRendererSettings& settings_;
    const DrawableProcessor& drawableProcessor_;
    InstancingBuffer& instancingBuffer_;
    const FrameInfo& frameInfo_;
    // TODO(renderer): Make it immutable so we can safely execute this code in multiple threads
    Scene& scene_;
    const ea::vector<LightProcessor*>& lights_;
    const Node& cameraNode_;
    /// @}

    struct EnabledFeatureFlags
    {
        EnabledFeatureFlags(BatchRenderFlags flags, InstancingBuffer& instancingBuffer)
            : ambientLighting_(flags.Test(BatchRenderFlag::EnableAmbientLighting))
            , vertexLighting_(flags.Test(BatchRenderFlag::EnableVertexLights))
            , pixelLighting_(flags.Test(BatchRenderFlag::EnablePixelLights))
            , anyLighting_(ambientLighting_ || vertexLighting_ || pixelLighting_)
            , staticInstancing_(instancingBuffer.IsEnabled()
                && flags.Test(BatchRenderFlag::EnableInstancingForStaticGeometry))
        {
        }

        bool ambientLighting_;
        bool vertexLighting_;
        bool pixelLighting_;
        bool anyLighting_;
        bool staticInstancing_;
    } const enabled_;

    struct DirtyStateFlags
    {
        bool pipelineState_{};
        bool frameConstants_{ true };
        bool cameraConstants_{ true };

        bool pixelLightConstants_{};
        bool pixelLightTextures_{};

        bool vertexLightConstants_{};

        bool lightmapConstants_{};
        bool lightmapTextures_{};

        bool material_{};
        bool geometry_{};

        bool IsAnyStateDirty() const
        {
            return pipelineState_ || frameConstants_ || cameraConstants_
                || pixelLightConstants_ || pixelLightTextures_ || vertexLightConstants_
                || lightmapConstants_ || lightmapTextures_ || material_ || geometry_;
        }
    } dirty_;

    struct CachedSharedState
    {
        PipelineState* pipelineState_{};
        float constantDepthBias_{};

        unsigned pixelLightIndex_{ M_MAX_UNSIGNED };
        bool pixelLightEnabled_{};
        const CookedLightParams* pixelLightParams_{};
        Texture* pixelLightRamp_{};
        Texture* pixelLightShape_{};
        Texture* pixelLightShadowMap_{};

        LightAccumulator::VertexLightContainer vertexLights_{};
        Vector4 vertexLightsData_[MAX_VERTEX_LIGHTS * 3]{};

        Texture* lightmapTexture_{};
        const Vector4* lightmapScaleOffset_{};

        Material* material_{};
        Geometry* geometry_{};
    } current_;

    struct ObjectState
    {
        const SphericalHarmonicsDot9* sh_{};
        Vector4 ambient_;
        GeometryType geometryType_{};
        const Matrix3x4* worldTransform_{};
        unsigned numWorldTransforms_{};
    } object_;

    struct InstancingGroupState
    {
        Geometry* geometry_{};
        unsigned start_{};
        unsigned count_{};
    } instancingGroup_;

    struct LightVolumeBatchHelpers
    {
        Matrix3x4 transform_;
        SourceBatch sourceBatch_;
    } lightVolumeHelpers_;
};

}

BatchRenderingContext::BatchRenderingContext(DrawCommandQueue& drawQueue, const Camera& camera)
    : drawQueue_(drawQueue)
    , camera_(camera)
{
}

BatchRenderingContext::BatchRenderingContext(DrawCommandQueue& drawQueue, const ShadowSplitProcessor& outputShadowSplit)
    : drawQueue_(drawQueue)
    , camera_(*outputShadowSplit.GetShadowCamera())
    , outputShadowSplit_(&outputShadowSplit)
{
}

BatchRenderer::BatchRenderer(Context* context, const DrawableProcessor* drawableProcessor,
    InstancingBuffer* instancingBuffer)
    : Object(context)
    , renderer_(context_->GetSubsystem<Renderer>())
    , drawableProcessor_(drawableProcessor)
    , instancingBuffer_(instancingBuffer)
{
}

void BatchRenderer::SetSettings(const BatchRendererSettings& settings)
{
    settings_ = settings;
}

void BatchRenderer::RenderBatches(const BatchRenderingContext& ctx,
    BatchRenderFlags flags, ea::span<const PipelineBatchByState> batches)
{
    DrawCommandCompositor compositor(ctx, settings_, *drawableProcessor_, *instancingBuffer_, flags);

    for (const auto& sortedBatch : batches)
        compositor.ProcessSceneBatch(*sortedBatch.pipelineBatch_);
    compositor.FlushDrawCommands();
}

void BatchRenderer::RenderBatches(const BatchRenderingContext& ctx,
    BatchRenderFlags flags, ea::span<const PipelineBatchBackToFront> batches)
{
    DrawCommandCompositor compositor(ctx, settings_, *drawableProcessor_, *instancingBuffer_, flags);

    for (const auto& sortedBatch : batches)
        compositor.ProcessSceneBatch(*sortedBatch.pipelineBatch_);
    compositor.FlushDrawCommands();
}

void BatchRenderer::RenderLightVolumeBatches(const BatchRenderingContext& ctx,
    ea::span<const PipelineBatchByState> batches)
{
    DrawCommandCompositor compositor(ctx, settings_, *drawableProcessor_, *instancingBuffer_,
        BatchRenderFlag::EnablePixelLights);

    for (const auto& sortedBatch : batches)
        compositor.ProcessLightVolumeBatch(*sortedBatch.pipelineBatch_);
    compositor.FlushDrawCommands();
}

}
