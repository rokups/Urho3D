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
#include "../Graphics/Zone.h"
#include "../IO/Log.h"
#include "../RenderPipeline/BatchRenderer.h"
#include "../RenderPipeline/DrawableProcessor.h"
#include "../RenderPipeline/InstancingBufferCompositor.h"
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
Vector4 GetCameraDepthModeParameter(const Camera* camera)
{
    Vector4 depthMode = Vector4::ZERO;
    if (camera->IsOrthographic())
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
        depthMode.w_ = 1.0f / camera->GetFarClip();
    return depthMode;
}

/// Return shader parameter for camera depth reconstruction.
Vector4 GetCameraDepthReconstructParameter(const Camera* camera)
{
    const float nearClip = camera->GetNearClip();
    const float farClip = camera->GetFarClip();
    return {
        farClip / (farClip - nearClip),
        -nearClip / (farClip - nearClip),
        camera->IsOrthographic() ? 1.0f : 0.0f,
        camera->IsOrthographic() ? 0.0f : 1.0f
    };
}

/// Return shader parameter for zone fog.
Vector4 GetFogParameter(const Camera* camera)
{
    const float farClip = camera->GetFarClip();
    float fogStart = Min(camera->GetEffectiveFogStart(), farClip);
    float fogEnd = Min(camera->GetEffectiveFogStart(), farClip);
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
class DrawCommandCompositor : public NonCopyable
{
public:
    /// Construct.
    DrawCommandCompositor(DrawCommandQueue& drawQueue, const BatchRendererSettings& settings,
        const DrawableProcessor& drawableProcessor, InstancingBufferCompositor& instancingBuffer,
        const Camera* renderCamera, BatchRenderFlags flags)
        : drawQueue_(drawQueue)
        , enableAmbientLight_(flags.Test(BatchRenderFlag::AmbientLight))
        , enableVertexLights_(flags.Test(BatchRenderFlag::VertexLights))
        , enablePixelLights_(flags.Test(BatchRenderFlag::PixelLight))
        , enableLight_(enableAmbientLight_ || enableVertexLights_ || enablePixelLights_)
        , enableStaticInstancing_(flags.Test(BatchRenderFlag::InstantiateStaticGeometry))
        , settings_(settings)
        , drawableProcessor_(drawableProcessor)
        , instancingBuffer_(instancingBuffer)
        , frameInfo_(drawableProcessor_.GetFrameInfo())
        , scene_(frameInfo_.scene_)
        , lights_(drawableProcessor_.GetLightProcessors())
        , camera_(renderCamera)
        , cameraNode_(camera_->GetNode())
    {
        lightVolumeSourceBatch_.worldTransform_ = &lightVolumeTransform_;
    }

    /// Set GBuffer parameters.
    void SetGBufferParameters(const Vector4& offsetAndScale, const Vector2& invSize)
    {
        geometryBufferOffsetAndScale_ = offsetAndScale;
        geometryBufferInvSize_ = invSize;
    }

    /// Set global resources.
    void SetGlobalResources(ea::span<const ShaderResourceDesc> globalResources)
    {
        globalResources_ = globalResources;
    }

    /// Convert scene batch to DrawCommandQueue commands.
    void ProcessSceneBatch(const PipelineBatch& pipelineBatch)
    {
        ProcessBatch(pipelineBatch, pipelineBatch.GetSourceBatch());
    }

    /// Convert light batch to DrawCommandQueue commands.
    void ProcessLightVolumeBatch(const PipelineBatch& pipelineBatch)
    {
        Light* light = lights_[pipelineBatch.lightIndex_]->GetLight();
        lightVolumeTransform_ = light->GetVolumeTransform(camera_);
        ProcessBatch(pipelineBatch, lightVolumeSourceBatch_);
    }

    /// Flush pending draw queue commands.
    void FlushDrawCommands()
    {
        if (instanceCount_ > 0)
        {
            drawQueue_.SetBuffers({ lastGeometry_->GetVertexBuffers(), lastGeometry_->GetIndexBuffer(),
                instancingBuffer_.GetVertexBuffer() });
            drawQueue_.DrawIndexedInstanced(lastGeometry_->GetIndexStart(), lastGeometry_->GetIndexCount(),
                startInstance_, instanceCount_);
            instanceCount_ = 0;
        }
    }

private:
    /// TODO(renderer): Refactor me
    Vector3 AdjustSHAmbient(const Vector3& x) const { return settings_.gammaCorrection_ ? x : Color(x).LinearToGamma().ToVector3(); };

    /// Add Frame shader parameters.
    void AddFrameShaderParameters()
    {
        drawQueue_.AddShaderParameter(VSP_DELTATIME, frameInfo_.timeStep_);
        drawQueue_.AddShaderParameter(VSP_ELAPSEDTIME, scene_->GetElapsedTime());
    }

    /// Add Camera shader parameters.
    void AddCameraShaderParameters(float constantDepthBias)
    {
        drawQueue_.AddShaderParameter(VSP_GBUFFEROFFSETS, geometryBufferOffsetAndScale_);
        drawQueue_.AddShaderParameter(PSP_GBUFFERINVSIZE, geometryBufferInvSize_);

        const Matrix3x4 cameraEffectiveTransform = camera_->GetEffectiveWorldTransform();
        drawQueue_.AddShaderParameter(VSP_CAMERAPOS, cameraEffectiveTransform.Translation());
        drawQueue_.AddShaderParameter(VSP_VIEWINV, cameraEffectiveTransform);
        drawQueue_.AddShaderParameter(VSP_VIEW, camera_->GetView());
        drawQueue_.AddShaderParameter(PSP_CAMERAPOS, cameraEffectiveTransform.Translation());

        const float nearClip = camera_->GetNearClip();
        const float farClip = camera_->GetFarClip();
        drawQueue_.AddShaderParameter(VSP_NEARCLIP, nearClip);
        drawQueue_.AddShaderParameter(VSP_FARCLIP, farClip);
        drawQueue_.AddShaderParameter(PSP_NEARCLIP, nearClip);
        drawQueue_.AddShaderParameter(PSP_FARCLIP, farClip);

        drawQueue_.AddShaderParameter(VSP_DEPTHMODE, GetCameraDepthModeParameter(camera_));
        drawQueue_.AddShaderParameter(PSP_DEPTHRECONSTRUCT, GetCameraDepthReconstructParameter(camera_));

        Vector3 nearVector, farVector;
        camera_->GetFrustumSize(nearVector, farVector);
        drawQueue_.AddShaderParameter(VSP_FRUSTUMSIZE, farVector);

        drawQueue_.AddShaderParameter(VSP_VIEWPROJ, camera_->GetEffectiveGPUViewProjection(constantDepthBias));

        const Color ambientColorGamma = camera_->GetEffectiveAmbientColor() * camera_->GetEffectiveAmbientBrightness();
        drawQueue_.AddShaderParameter(PSP_AMBIENTCOLOR,
            settings_.gammaCorrection_ ? ambientColorGamma.GammaToLinear() : ambientColorGamma);
        drawQueue_.AddShaderParameter(PSP_FOGCOLOR, camera_->GetEffectiveFogColor());
        drawQueue_.AddShaderParameter(PSP_FOGPARAMS, GetFogParameter(camera_));
    }

    /// Cache vertex lights data.
    void CacheVertexLights(const LightAccumulator::VertexLightContainer& vertexLights)
    {
        for (unsigned i = 0; i < MAX_VERTEX_LIGHTS; ++i)
        {
            if (vertexLights[i] == M_MAX_UNSIGNED)
            {
                vertexLightsData_[i * 3] = {};
                vertexLightsData_[i * 3 + 1] = {};
                vertexLightsData_[i * 3 + 2] = {};
            }
            else
            {
                const LightShaderParameters& vertexLightParams = lights_[vertexLights[i]]->GetShaderParams();
                vertexLightsData_[i * 3] = { vertexLightParams.GetColor(settings_.gammaCorrection_), vertexLightParams.invRange_ };
                vertexLightsData_[i * 3 + 1] = { vertexLightParams.direction_, vertexLightParams.cutoff_ };
                vertexLightsData_[i * 3 + 2] = { vertexLightParams.position_, vertexLightParams.invCutoff_ };
            }
        }
    }

    /// Cache pixel light.
    void CachePixelLight(unsigned lightIndex)
    {
        hasPixelLight_ = lightIndex != M_MAX_UNSIGNED;
        lightParams_ = hasPixelLight_ ? &lights_[lightIndex]->GetShaderParams() : nullptr;
    }

    /// Add Light shader parameters.
    void AddLightShaderParameters()
    {
        // Add vertex lights parameters
        drawQueue_.AddShaderParameter(VSP_VERTEXLIGHTS, ea::span<const Vector4>{ vertexLightsData_ });

        // Add pixel light parameters
        if (!hasPixelLight_)
            return;

        drawQueue_.AddShaderParameter(VSP_LIGHTDIR, lightParams_->direction_);
        drawQueue_.AddShaderParameter(VSP_LIGHTPOS,
            Vector4{ lightParams_->position_, lightParams_->invRange_ });
        drawQueue_.AddShaderParameter(PSP_LIGHTCOLOR,
            Vector4{ lightParams_->GetColor(settings_.gammaCorrection_), lightParams_->specularIntensity_ });

        drawQueue_.AddShaderParameter(PSP_LIGHTRAD, lightParams_->radius_);
        drawQueue_.AddShaderParameter(PSP_LIGHTLENGTH, lightParams_->length_);

        if (lightParams_->numLightMatrices_ > 0)
        {
            ea::span<const Matrix4> shadowMatricesSpan{ lightParams_->lightMatrices_, lightParams_->numLightMatrices_ };
            drawQueue_.AddShaderParameter(VSP_LIGHTMATRICES, shadowMatricesSpan);
        }

        if (lightParams_->shadowMap_)
        {
            drawQueue_.AddShaderParameter(PSP_SHADOWDEPTHFADE, lightParams_->shadowDepthFade_);
            drawQueue_.AddShaderParameter(PSP_SHADOWINTENSITY, lightParams_->shadowIntensity_);
            drawQueue_.AddShaderParameter(PSP_SHADOWMAPINVSIZE, lightParams_->shadowMapInvSize_);
            drawQueue_.AddShaderParameter(PSP_SHADOWSPLITS, lightParams_->shadowSplits_);
            drawQueue_.AddShaderParameter(PSP_SHADOWCUBEUVBIAS, lightParams_->shadowCubeUVBias_);
            drawQueue_.AddShaderParameter(PSP_SHADOWCUBEADJUST, lightParams_->shadowCubeAdjust_);
            drawQueue_.AddShaderParameter(VSP_NORMALOFFSETSCALE, lightParams_->normalOffsetScale_);
            drawQueue_.AddShaderParameter(PSP_VSMSHADOWPARAMS, settings_.vsmShadowParams_);
        }
    }

    /// Convert PipelineBatch to DrawCommandQueue commands.
    void ProcessBatch(const PipelineBatch& pipelineBatch, const SourceBatch& sourceBatch)
    {
        const LightAccumulator* lightAccumulator = enableAmbientLight_ || enableVertexLights_
            ? &drawableProcessor_.GetGeometryLighting(pipelineBatch.drawableIndex_)
            : nullptr;

        // Check if pipeline state changed
        const bool pipelineStateDirty = pipelineBatch.pipelineState_ != lastPipelineState_;
        if (pipelineStateDirty)
            lastPipelineState_ = pipelineBatch.pipelineState_;

        // Check if camera parameters changed
        const float constantDepthBias = pipelineBatch.pipelineState_->GetDesc().constantDepthBias_;
        if (lastConstantDepthBias_ != constantDepthBias)
        {
            lastConstantDepthBias_ = constantDepthBias;
            cameraDirty_ = true;
        }

        // Check if pixel lights changed
        bool pixelLightDirty = false;
        if (enablePixelLights_)
        {
            pixelLightDirty = pipelineBatch.lightIndex_ != lastLightIndex_;

            if (pixelLightDirty)
            {
                CachePixelLight(pipelineBatch.lightIndex_);
                lastLightIndex_ = pipelineBatch.lightIndex_;
            }
        }

        // Check if vertex lights changed
        bool vertexLightsDirty = false;
        if (enableVertexLights_)
        {
            vertexLights_ = lightAccumulator->GetVertexLights();
            vertexLightsDirty = lastVertexLights_ != vertexLights_;

            if (vertexLightsDirty)
            {
                CacheVertexLights(vertexLights_);
                lastVertexLights_ = vertexLights_;
            }
        }

        // Check if material is dirty
        const bool materialDirty = lastMaterial_ != pipelineBatch.material_;
        if (materialDirty)
            lastMaterial_ = pipelineBatch.material_;

        // Check if geometry is dirty
        const bool geometryDirty = lastGeometry_ != pipelineBatch.geometry_;

        // Check if batch should be appended to ongoing instanced draw operation
        const bool appendInstancedBatch = instanceCount_ > 0
            && !pipelineStateDirty && !pixelLightDirty && !vertexLightsDirty && !materialDirty && !geometryDirty;

        // Update state if not instanced batch
        if (!appendInstancedBatch)
        {
            // Flush draw commands if instancing draw was in progress
            FlushDrawCommands();

            // TODO(renderer): Cleanup me
            if (geometryDirty)
                lastGeometry_ = pipelineBatch.geometry_;

            // Initialize pipeline state
            if (pipelineStateDirty)
                drawQueue_.SetPipelineState(pipelineBatch.pipelineState_);

            // Add frame shader parameters
            if (drawQueue_.BeginShaderParameterGroup(SP_FRAME, frameDirty_))
            {
                AddFrameShaderParameters();
                drawQueue_.CommitShaderParameterGroup(SP_FRAME);
                frameDirty_ = false;
            }

            // Add camera shader parameters
            if (drawQueue_.BeginShaderParameterGroup(SP_CAMERA, cameraDirty_))
            {
                AddCameraShaderParameters(constantDepthBias);
                drawQueue_.CommitShaderParameterGroup(SP_CAMERA);
                cameraDirty_ = false;
            }

            // Add light shader parameters
            if (enableLight_ && drawQueue_.BeginShaderParameterGroup(SP_LIGHT, pixelLightDirty || vertexLightsDirty))
            {
                AddLightShaderParameters();
                drawQueue_.CommitShaderParameterGroup(SP_LIGHT);
            }

            // Add material shader parameters
            // TODO(renderer): Check for sourceBatch.lightmapScaleOffset_ dirty?
            if (drawQueue_.BeginShaderParameterGroup(SP_MATERIAL, materialDirty || sourceBatch.lightmapScaleOffset_))
            {
                const auto& materialParameters = pipelineBatch.material_->GetShaderParameters();
                for (const auto& parameter : materialParameters)
                    drawQueue_.AddShaderParameter(parameter.first, parameter.second.value_);
                if (enableAmbientLight_ && sourceBatch.lightmapScaleOffset_)
                    drawQueue_.AddShaderParameter(VSP_LMOFFSET, *sourceBatch.lightmapScaleOffset_);
                drawQueue_.CommitShaderParameterGroup(SP_MATERIAL);
            }

            // Add resources
            // TODO(renderer): Don't check for pixelLightDirty, check for shadow map and ramp/shape only
            if (materialDirty || pixelLightDirty || sourceBatch.lightmapScaleOffset_)
            {
                // Add global resources
                for (const ShaderResourceDesc& desc : globalResources_)
                    drawQueue_.AddShaderResource(desc.unit_, desc.texture_);

                // Add material resources
                const auto& materialTextures = pipelineBatch.material_->GetTextures();
                for (const auto& texture : materialTextures)
                    drawQueue_.AddShaderResource(texture.first, texture.second);

                // Add lightmap
                if (enableAmbientLight_)
                    drawQueue_.AddShaderResource(TU_EMISSIVE, scene_->GetLightmapTexture(sourceBatch.lightmapIndex_));

                // Add light resources
                if (hasPixelLight_)
                {
                    drawQueue_.AddShaderResource(TU_LIGHTRAMP, lightParams_->lightRamp_);
                    drawQueue_.AddShaderResource(TU_LIGHTSHAPE, lightParams_->lightShape_);
                    if (lightParams_->shadowMap_)
                        drawQueue_.AddShaderResource(TU_SHADOWMAP, lightParams_->shadowMap_);
                }

                drawQueue_.CommitShaderResources();
            }

            // Add object shader parameters
            const bool useInstancingBuffer = enableStaticInstancing_
                && pipelineBatch.geometryType_ == GEOM_STATIC
                && !!pipelineBatch.geometry_->GetIndexBuffer();
            if (useInstancingBuffer)
            {
                // TODO(renderer): Refactor me
                instanceCount_ = 1;
                startInstance_ = instancingBuffer_.AddInstance();
                instancingBuffer_.SetElements(sourceBatch.worldTransform_, 0, 3);
                if (enableAmbientLight_)
                {
                    const SphericalHarmonicsDot9& sh = lightAccumulator->sphericalHarmonics_;
                    const Vector4 ambient(AdjustSHAmbient(sh.EvaluateAverage()), 1.0f);
                    if (settings_.ambientMode_ == AmbientMode::Flat)
                        instancingBuffer_.SetElements(&ambient, 3, 1);
                    else if (settings_.ambientMode_ == AmbientMode::Directional)
                        instancingBuffer_.SetElements(&sh, 3, 7);
                }
            }
            else if (drawQueue_.BeginShaderParameterGroup(SP_OBJECT, true))
            {
                // Add ambient light parameters
                if (enableAmbientLight_)
                {
                    const SphericalHarmonicsDot9& sh = lightAccumulator->sphericalHarmonics_;
                    drawQueue_.AddShaderParameter(VSP_SHAR, sh.Ar_);
                    drawQueue_.AddShaderParameter(VSP_SHAG, sh.Ag_);
                    drawQueue_.AddShaderParameter(VSP_SHAB, sh.Ab_);
                    drawQueue_.AddShaderParameter(VSP_SHBR, sh.Br_);
                    drawQueue_.AddShaderParameter(VSP_SHBG, sh.Bg_);
                    drawQueue_.AddShaderParameter(VSP_SHBB, sh.Bb_);
                    drawQueue_.AddShaderParameter(VSP_SHC, sh.C_);
                    drawQueue_.AddShaderParameter(VSP_AMBIENT, Vector4(AdjustSHAmbient(sh.EvaluateAverage()), 1.0f));
                }

                // Add transform parameters
                switch (pipelineBatch.geometryType_)
                {
                case GEOM_INSTANCED:
                    // TODO(renderer): Do something with this branch
                    assert(0);
                    break;
                case GEOM_SKINNED:
                    drawQueue_.AddShaderParameter(VSP_SKINMATRICES,
                        ea::span<const Matrix3x4>(sourceBatch.worldTransform_, sourceBatch.numWorldTransforms_));
                    break;
                case GEOM_BILLBOARD:
                    drawQueue_.AddShaderParameter(VSP_MODEL, *sourceBatch.worldTransform_);
                    if (sourceBatch.numWorldTransforms_ > 1)
                        drawQueue_.AddShaderParameter(VSP_BILLBOARDROT, sourceBatch.worldTransform_[1].RotationMatrix());
                    else
                        drawQueue_.AddShaderParameter(VSP_BILLBOARDROT, cameraNode_->GetWorldRotation().RotationMatrix());
                    break;
                default:
                    drawQueue_.AddShaderParameter(VSP_MODEL, *sourceBatch.worldTransform_);
                    break;
                }
                drawQueue_.CommitShaderParameterGroup(SP_OBJECT);

                // Set buffers and draw
                drawQueue_.SetBuffers({ pipelineBatch.geometry_->GetVertexBuffers(), pipelineBatch.geometry_->GetIndexBuffer(), nullptr });
                drawQueue_.DrawIndexed(pipelineBatch.geometry_->GetIndexStart(), pipelineBatch.geometry_->GetIndexCount());
            }
        }
        else
        {
            // TODO(renderer): Refactor me
            instanceCount_ += 1;
            instancingBuffer_.AddInstance();
            instancingBuffer_.SetElements(sourceBatch.worldTransform_, 0, 3);
            if (enableAmbientLight_)
            {
                const SphericalHarmonicsDot9& sh = lightAccumulator->sphericalHarmonics_;
                const Vector4 ambient(AdjustSHAmbient(sh.EvaluateAverage()), 1.0f);
                if (settings_.ambientMode_ == AmbientMode::Flat)
                    instancingBuffer_.SetElements(&ambient, 3, 1);
                else if (settings_.ambientMode_ == AmbientMode::Directional)
                    instancingBuffer_.SetElements(&sh, 3, 7);
            }
        }
    }

    /// Destination draw queue.
    DrawCommandQueue& drawQueue_;

    /// Export ambient light and vertex lights.
    const bool enableAmbientLight_;
    /// Export ambient light and vertex lights.
    const bool enableVertexLights_;
    /// Export pixel light.
    const bool enablePixelLights_;
    /// Export light of any kind.
    const bool enableLight_;
    /// Use instancing buffer for object parameters of static geometry.
    const bool enableStaticInstancing_;

    /// Settings.
    const BatchRendererSettings& settings_;

    /// Drawable processor.
    const DrawableProcessor& drawableProcessor_;
    /// Instancing buffer.
    InstancingBufferCompositor& instancingBuffer_;
    /// Frame info.
    const FrameInfo& frameInfo_;
    /// Scene.
    Scene* const scene_{};
    /// Light array for indexing.
    const ea::vector<LightProcessor*>& lights_;
    /// Render camera.
    const Camera* const camera_{};
    /// Render camera node.
    const Node* const cameraNode_{};

    /// Offset and scale of GBuffer.
    Vector4 geometryBufferOffsetAndScale_;
    /// Inverse size of GBuffer.
    Vector2 geometryBufferInvSize_;

    /// Global resources.
    ea::span<const ShaderResourceDesc> globalResources_;

    /// Last used pipeline state.
    PipelineState* lastPipelineState_{};

    /// Whether frame shader parameters are dirty.
    bool frameDirty_{ true };
    /// Whether camera shader parameters are dirty.
    bool cameraDirty_{ true };
    /// Last used constant depth bias. Ignored on first frame.
    float lastConstantDepthBias_{ 0.0f };

    /// Last used pixel light.
    unsigned lastLightIndex_{ M_MAX_UNSIGNED };
    /// Whether pixel light is active.
    bool hasPixelLight_{};
    /// Pixel light paramters.
    const LightShaderParameters* lightParams_{};

    /// Last used vertex lights.
    LightAccumulator::VertexLightContainer lastVertexLights_{};
    /// Current vertex lights.
    LightAccumulator::VertexLightContainer vertexLights_{};
    /// Cached vertex lights data.
    Vector4 vertexLightsData_[MAX_VERTEX_LIGHTS * 3]{};

    /// Last used material.
    Material* lastMaterial_{};
    /// Last used geometry.
    Geometry* lastGeometry_{};

    /// Temporary world transform for light volumes.
    Matrix3x4 lightVolumeTransform_;
    /// Temporary source batch for light volumes.
    SourceBatch lightVolumeSourceBatch_;

    /// Start instance for pending call.
    unsigned startInstance_{};
    /// Number of instances in call.
    unsigned instanceCount_{};
};

}

BatchRenderer::BatchRenderer(Context* context, const DrawableProcessor* drawableProcessor,
    InstancingBufferCompositor* instancingBuffer)
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

void BatchRenderer::RenderBatches(DrawCommandQueue& drawQueue, const Camera* camera, BatchRenderFlags flags,
    ea::span<const PipelineBatchByState> batches)
{
    DrawCommandCompositor compositor(drawQueue, settings_,
        *drawableProcessor_, *instancingBuffer_, camera, flags);

    for (const auto& sortedBatch : batches)
        compositor.ProcessSceneBatch(*sortedBatch.pipelineBatch_);
    compositor.FlushDrawCommands();
}

void BatchRenderer::RenderBatches(DrawCommandQueue& drawQueue, const Camera* camera, BatchRenderFlags flags,
    ea::span<const PipelineBatchBackToFront> batches)
{
    DrawCommandCompositor compositor(drawQueue, settings_,
        *drawableProcessor_, *instancingBuffer_, camera, flags);

    for (const auto& sortedBatch : batches)
        compositor.ProcessSceneBatch(*sortedBatch.pipelineBatch_);
    compositor.FlushDrawCommands();
}

void BatchRenderer::RenderLightVolumeBatches(DrawCommandQueue& drawQueue, Camera* camera,
    const LightVolumeRenderContext& ctx, ea::span<const PipelineBatchByState> batches)
{
    DrawCommandCompositor compositor(drawQueue, settings_,
        *drawableProcessor_, *instancingBuffer_, camera, BatchRenderFlag::PixelLight);

    compositor.SetGBufferParameters(ctx.geometryBufferOffsetAndScale_, ctx.geometryBufferInvSize_);
    compositor.SetGlobalResources(ctx.geometryBuffer_);

    for (const auto& sortedBatch : batches)
        compositor.ProcessLightVolumeBatch(*sortedBatch.pipelineBatch_);
    compositor.FlushDrawCommands();
}

}
