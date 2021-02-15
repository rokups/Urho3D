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
#include "../IO/Log.h"
#include "../Graphics/Camera.h"
#include "../Graphics/DrawCommandQueue.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Octree.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/Zone.h"
#include "../RenderPipeline/DrawableProcessor.h"
#include "../RenderPipeline/LightProcessor.h"
#include "../RenderPipeline/BatchRenderer.h"
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

/// Add frame-specific shader parameters.
void AddFrameShaderParameters(DrawCommandQueue& drawQueue, const FrameInfo& frameInfo, const Scene* scene)
{
    drawQueue.AddShaderParameter(VSP_DELTATIME, frameInfo.timeStep_);
    drawQueue.AddShaderParameter(PSP_DELTATIME, frameInfo.timeStep_);

    const float elapsedTime = scene->GetElapsedTime();
    drawQueue.AddShaderParameter(VSP_ELAPSEDTIME, elapsedTime);
    drawQueue.AddShaderParameter(PSP_ELAPSEDTIME, elapsedTime);
}

/// Add camera-specific shader parameters.
void AddCameraShaderParameters(DrawCommandQueue& drawQueue, const Camera* camera, float constantDepthBias)
{
    const Matrix3x4 cameraEffectiveTransform = camera->GetEffectiveWorldTransform();
    drawQueue.AddShaderParameter(VSP_CAMERAPOS, cameraEffectiveTransform.Translation());
    drawQueue.AddShaderParameter(VSP_VIEWINV, cameraEffectiveTransform);
    drawQueue.AddShaderParameter(VSP_VIEW, camera->GetView());
    drawQueue.AddShaderParameter(PSP_CAMERAPOS, cameraEffectiveTransform.Translation());

    const float nearClip = camera->GetNearClip();
    const float farClip = camera->GetFarClip();
    drawQueue.AddShaderParameter(VSP_NEARCLIP, nearClip);
    drawQueue.AddShaderParameter(VSP_FARCLIP, farClip);
    drawQueue.AddShaderParameter(PSP_NEARCLIP, nearClip);
    drawQueue.AddShaderParameter(PSP_FARCLIP, farClip);

    drawQueue.AddShaderParameter(VSP_DEPTHMODE, GetCameraDepthModeParameter(camera));
    drawQueue.AddShaderParameter(PSP_DEPTHRECONSTRUCT, GetCameraDepthReconstructParameter(camera));

    Vector3 nearVector, farVector;
    camera->GetFrustumSize(nearVector, farVector);
    drawQueue.AddShaderParameter(VSP_FRUSTUMSIZE, farVector);

    drawQueue.AddShaderParameter(VSP_VIEWPROJ, camera->GetEffectiveGPUViewProjection(constantDepthBias));

    drawQueue.AddShaderParameter(PSP_AMBIENTCOLOR, camera->GetEffectiveAmbientColor());
    drawQueue.AddShaderParameter(PSP_FOGCOLOR, camera->GetEffectiveFogColor());
    drawQueue.AddShaderParameter(PSP_FOGPARAMS, GetFogParameter(camera));
}

/// Batch renderer to command queue.
// TODO(renderer): Add template parameter to avoid branching?
class DrawCommandCompositor : public NonCopyable
{
public:
    /// Construct.
    DrawCommandCompositor(DrawCommandQueue& drawQueue,
        const DrawableProcessor& drawableProcessor, const Camera* renderCamera, BatchRenderFlags flags)
        : drawQueue_(drawQueue)
        , enableAmbientAndVertexLights_(flags.Test(BatchRenderFlag::AmbientAndVertexLights))
        , enablePixelLights_(flags.Test(BatchRenderFlag::PixelLight))
        , enableLight_(enableAmbientAndVertexLights_ || enablePixelLights_)
        , drawableProcessor_(drawableProcessor)
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

private:
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

        drawQueue_.AddShaderParameter(PSP_AMBIENTCOLOR, camera_->GetEffectiveAmbientColor());
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
                vertexLightsData_[i * 3] = { vertexLightParams.color_, vertexLightParams.invRange_ };
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
            Vector4{ lightParams_->color_, lightParams_->specularIntensity_ });

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
            // TODO(renderer): Add VSM
            //drawQueue_.AddShaderParameter(PSP_VSMSHADOWPARAMS, renderer_->GetVSMShadowParameters());
        }
    }

    /// Convert PipelineBatch to DrawCommandQueue commands.
    void ProcessBatch(const PipelineBatch& pipelineBatch, const SourceBatch& sourceBatch)
    {
        const LightAccumulator* lightAccumulator = enableAmbientAndVertexLights_
            ? &drawableProcessor_.GetGeometryLighting(pipelineBatch.drawableIndex_)
            : nullptr;

        // Initialize pipeline state
        drawQueue_.SetPipelineState(pipelineBatch.pipelineState_);

        // Add frame shader parameters
        if (drawQueue_.BeginShaderParameterGroup(SP_FRAME, frameDirty_))
        {
            AddFrameShaderParameters();
            drawQueue_.CommitShaderParameterGroup(SP_FRAME);
            frameDirty_ = false;
        }

        // Add camera shader parameters
        const float constantDepthBias = pipelineBatch.pipelineState_->GetDesc().constantDepthBias_;
        if (drawQueue_.BeginShaderParameterGroup(SP_CAMERA,
            cameraDirty_ || lastConstantDepthBias_ != constantDepthBias))
        {
            AddCameraShaderParameters(constantDepthBias);
            drawQueue_.CommitShaderParameterGroup(SP_CAMERA);
            cameraDirty_ = false;
            lastConstantDepthBias_ = constantDepthBias;
        }

        // Check if lights are dirty
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

        bool vertexLightsDirty = false;
        if (enableAmbientAndVertexLights_)
        {
            vertexLights_ = lightAccumulator->GetVertexLights();
            ea::sort(vertexLights_.begin(), vertexLights_.end());
            vertexLightsDirty = lastVertexLights_ != vertexLights_;

            if (vertexLightsDirty)
            {
                CacheVertexLights(vertexLights_);
                lastVertexLights_ = vertexLights_;
            }
        }

        // Add light shader parameters
        if (enableLight_ && drawQueue_.BeginShaderParameterGroup(SP_LIGHT, pixelLightDirty || vertexLightsDirty))
        {
            AddLightShaderParameters();
            drawQueue_.CommitShaderParameterGroup(SP_LIGHT);
        }

        // Check if material is dirty
        bool materialDirty = false;
        if (lastMaterial_ != pipelineBatch.material_)
        {
            materialDirty = true;
            lastMaterial_ = pipelineBatch.material_;
        }

        // Add material shader parameters
        if (drawQueue_.BeginShaderParameterGroup(SP_MATERIAL, materialDirty))
        {
            const auto& materialParameters = pipelineBatch.material_->GetShaderParameters();
            for (const auto& parameter : materialParameters)
                drawQueue_.AddShaderParameter(parameter.first, parameter.second.value_);
            drawQueue_.CommitShaderParameterGroup(SP_MATERIAL);
        }

        // Add resources
        // TODO(renderer): Don't check for pixelLightDirty, check for shadow map and ramp/shape only
        if (materialDirty || pixelLightDirty)
        {
            // Add global resources
            for (const ShaderResourceDesc& desc : globalResources_)
                drawQueue_.AddShaderResource(desc.unit_, desc.texture_);

            // Add material resources
            const auto& materialTextures = pipelineBatch.material_->GetTextures();
            for (const auto& texture : materialTextures)
                drawQueue_.AddShaderResource(texture.first, texture.second);

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
        if (drawQueue_.BeginShaderParameterGroup(SP_OBJECT, true))
        {
            // Add ambient light parameters
            if (enableAmbientAndVertexLights_)
            {
                const SphericalHarmonicsDot9& sh = lightAccumulator->sh_;
                drawQueue_.AddShaderParameter(VSP_SHAR, sh.Ar_);
                drawQueue_.AddShaderParameter(VSP_SHAG, sh.Ag_);
                drawQueue_.AddShaderParameter(VSP_SHAB, sh.Ab_);
                drawQueue_.AddShaderParameter(VSP_SHBR, sh.Br_);
                drawQueue_.AddShaderParameter(VSP_SHBG, sh.Bg_);
                drawQueue_.AddShaderParameter(VSP_SHBB, sh.Bb_);
                drawQueue_.AddShaderParameter(VSP_SHC, sh.C_);
                drawQueue_.AddShaderParameter(VSP_AMBIENT, Vector4(sh.EvaluateAverage(), 1.0f));
            }

            // Add transform parameters
            switch (pipelineBatch.geometryType_)
            {
            case GEOM_INSTANCED:
                // TODO(renderer): Implement instancing
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
        }

        // Set buffers and draw
        drawQueue_.SetBuffers(pipelineBatch.geometry_->GetVertexBuffers(), pipelineBatch.geometry_->GetIndexBuffer());
        drawQueue_.DrawIndexed(pipelineBatch.geometry_->GetIndexStart(), pipelineBatch.geometry_->GetIndexCount());
    }

    /// Destination draw queue.
    DrawCommandQueue& drawQueue_;

    /// Export ambient light and vertex lights.
    const bool enableAmbientAndVertexLights_;
    /// Export pixel light.
    const bool enablePixelLights_;
    /// Export light of any kind.
    const bool enableLight_;

    /// Drawable processor.
    const DrawableProcessor& drawableProcessor_;
    /// Frame info.
    const FrameInfo& frameInfo_;
    /// Scene.
    const Scene* scene_{};
    /// Light array for indexing.
    const ea::vector<LightProcessor*>& lights_;
    /// Render camera.
    const Camera* camera_{};
    /// Render camera node.
    const Node* cameraNode_{};

    /// Offset and scale of GBuffer.
    Vector4 geometryBufferOffsetAndScale_;
    /// Inverse size of GBuffer.
    Vector2 geometryBufferInvSize_;

    /// Global resources.
    ea::span<const ShaderResourceDesc> globalResources_;

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

    /// Temporary world transform for light volumes.
    Matrix3x4 lightVolumeTransform_;
    /// Temporary source batch for light volumes.
    SourceBatch lightVolumeSourceBatch_;
};

}

BatchRenderer::BatchRenderer(Context* context, const DrawableProcessor* drawableProcessor)
    : Object(context)
    , graphics_(context_->GetSubsystem<Graphics>())
    , renderer_(context_->GetSubsystem<Renderer>())
    , drawableProcessor_(drawableProcessor)
{
}

void BatchRenderer::RenderBatches(DrawCommandQueue& drawQueue, const Camera* camera, BatchRenderFlags flags,
    ea::span<const PipelineBatchByState> batches)
{
    DrawCommandCompositor compositor{ drawQueue, *drawableProcessor_, camera, flags };
    for (const auto& sortedBatch : batches)
        compositor.ProcessSceneBatch(*sortedBatch.sceneBatch_);
}

void BatchRenderer::RenderBatches(DrawCommandQueue& drawQueue, const Camera* camera, BatchRenderFlags flags,
    ea::span<const PipelineBatchBackToFront> batches)
{
    DrawCommandCompositor compositor{ drawQueue, *drawableProcessor_, camera, flags };
    for (const auto& sortedBatch : batches)
        compositor.ProcessSceneBatch(*sortedBatch.sceneBatch_);
}

void BatchRenderer::RenderUnlitBaseBatches(DrawCommandQueue& drawQueue, const DrawableProcessor& drawableProcessor,
    Camera* camera, Zone* zone, ea::span<const PipelineBatchByState> batches)
{
    //DrawCommandCompositor compositor{ drawQueue, drawableProcessor, camera, BatchRenderFlag::AmbientAndVertexLights };
    //for (const auto& sortedBatch : batches)
    //    compositor.ProcessBatch(*sortedBatch.sceneBatch_);

    //const auto getBatchLight = [&](const PipelineBatchByState& batch) { return nullptr; };
    //RenderBatches<false>(drawQueue, drawableProcessor, camera, zone, batches);
}

void BatchRenderer::RenderLitBaseBatches(DrawCommandQueue& drawQueue, const DrawableProcessor& drawableProcessor,
    Camera* camera, Zone* zone, ea::span<const PipelineBatchByState> batches)
{
    /*LightProcessor* mainLight = drawableProcessor.GetMainLight();
    const auto getBatchLight = [&](const PipelineBatchByState& batch)
    {
        return mainLight;
    };*/
    //RenderBatches<true>(drawQueue, drawableProcessor, camera, zone, batches);
}

/*void BatchRenderer::RenderLightBatches(DrawCommandQueue& drawQueue, const DrawableProcessor& drawableProcessor,
    Camera* camera, Zone* zone, ea::span<const LightBatchSortedByState> batches)
{
    const ea::vector<LightProcessor*>& visibleLights = drawableProcessor.GetVisibleLights();
    const auto getBatchLight = [&](const LightBatchSortedByState& batch)
    {
        return visibleLights[batch.lightIndex_];
    };
    RenderBatches<true>(drawQueue, drawableProcessor, camera, zone, batches, getBatchLight);
}*/

void BatchRenderer::RenderAlphaBatches(DrawCommandQueue& drawQueue, const DrawableProcessor& drawableProcessor,
    Camera* camera, Zone* zone, ea::span<const PipelineBatchBackToFront> batches)
{
    /*const ea::vector<LightProcessor*>& visibleLights = drawableProcessor.GetVisibleLights();
    const auto getBatchLight = [&](const PipelineBatchBackToFront& batch)
    {
        const unsigned lightIndex = batch.sceneBatch_->lightIndex_;
        return lightIndex != M_MAX_UNSIGNED ? visibleLights[lightIndex] : nullptr;
    };*/
    //RenderBatches<true>(drawQueue, drawableProcessor, camera, zone, batches);
}

void BatchRenderer::RenderShadowBatches(DrawCommandQueue& drawQueue, const DrawableProcessor& drawableProcessor,
    Camera* camera, Zone* zone, ea::span<const PipelineBatchByState> batches)
{
    //const auto getBatchLight = [&](const PipelineBatchByState& batch) { return nullptr; };
    //RenderBatches<false>(drawQueue, drawableProcessor, camera, zone, batches);
}

void BatchRenderer::RenderLightVolumeBatches(DrawCommandQueue& drawQueue, Camera* camera,
    const LightVolumeRenderContext& ctx, ea::span<const PipelineBatchByState> batches)
{
    DrawCommandCompositor compositor{ drawQueue, *drawableProcessor_, camera, BatchRenderFlag::PixelLight };
    compositor.SetGBufferParameters(ctx.geometryBufferOffsetAndScale_, ctx.geometryBufferInvSize_);
    compositor.SetGlobalResources(ctx.geometryBuffer_);
    for (const auto& sortedBatch : batches)
        compositor.ProcessLightVolumeBatch(*sortedBatch.sceneBatch_);
    return;

#if 0
    // TODO(renderer): Remove copypaste
    const FrameInfo& frameInfo = drawableProcessor.GetFrameInfo();
    const Scene* scene = frameInfo.octree_->GetScene();
    const ea::vector<LightProcessor*>& visibleLights = drawableProcessor.GetLightProcessors();
    Node* cameraNode = camera->GetNode();

    static const LightShaderParameters defaultLightParams;
    const LightShaderParameters* currentLightParams = &defaultLightParams;
    Texture2D* currentShadowMap = nullptr;

    bool frameDirty = true;
    bool cameraDirty = true;
    float previousConstantDepthBias = 0.0f;
    const LightProcessor* previousLight = nullptr;

    for (const PipelineBatch& batch : batches)
    {
        PipelineState* pipelineState = batch.pipelineState_;
        if (!pipelineState)
        {
            static std::atomic_flag errorReported;
            if (!errorReported.test_and_set())
                URHO3D_LOGERROR("Cannot render scene batch witout pipeline state");
            continue;
        }

        // Get used light
        const LightProcessor* light = visibleLights[batch.lightIndex_];
        const bool lightDirty = light != previousLight;
        if (lightDirty)
        {
            previousLight = light;
            currentLightParams = light ? &light->GetShaderParams() : &defaultLightParams;
            currentShadowMap = light ? light->GetShadowMap().texture_ : nullptr;
        }

        // Always set pipeline state first
        drawQueue.SetPipelineState(batch.pipelineState_);

        // Reset camera parameters if depth bias changed
        const float constantDepthBias = pipelineState->GetDesc().constantDepthBias_;

        // Add frame parameters
        if (drawQueue.BeginShaderParameterGroup(SP_FRAME, frameDirty))
        {
            AddFrameShaderParameters(drawQueue, frameInfo, scene);
            drawQueue.CommitShaderParameterGroup(SP_FRAME);
            frameDirty = false;
        }

        // Add camera parameters
        if (drawQueue.BeginShaderParameterGroup(SP_CAMERA,
            !cameraDirty || previousConstantDepthBias != constantDepthBias))
        {
            AddCameraShaderParameters(drawQueue, camera, constantDepthBias);
            drawQueue.AddShaderParameter(VSP_GBUFFEROFFSETS, geometryBufferOffset);
            drawQueue.AddShaderParameter(PSP_GBUFFERINVSIZE, geometryBufferInvSize);
            drawQueue.CommitShaderParameterGroup(SP_CAMERA);
            cameraDirty = false;
            previousConstantDepthBias = constantDepthBias;
        }

        // Add light parameters
        if constexpr (1)
        {
            if (drawQueue.BeginShaderParameterGroup(SP_LIGHT, lightDirty))
            {
                drawQueue.AddShaderParameter(VSP_LIGHTDIR, currentLightParams->direction_);
                drawQueue.AddShaderParameter(VSP_LIGHTPOS,
                    Vector4{ currentLightParams->position_, currentLightParams->invRange_ });
                drawQueue.AddShaderParameter(PSP_LIGHTCOLOR,
                    Vector4{ currentLightParams->color_, currentLightParams->specularIntensity_ });
                drawQueue.AddShaderParameter(PSP_LIGHTRAD, currentLightParams->radius_);
                drawQueue.AddShaderParameter(PSP_LIGHTLENGTH, currentLightParams->length_);

                if (currentLightParams->numLightMatrices_ > 0)
                {
                    ea::span<const Matrix4> shadowMatricesSpan{ currentLightParams->lightMatrices_, currentLightParams->numLightMatrices_ };
                    drawQueue.AddShaderParameter(VSP_LIGHTMATRICES, shadowMatricesSpan);
                }
                if (currentShadowMap)
                {
                    drawQueue.AddShaderParameter(PSP_SHADOWDEPTHFADE, currentLightParams->shadowDepthFade_);
                    drawQueue.AddShaderParameter(PSP_SHADOWINTENSITY, currentLightParams->shadowIntensity_);
                    drawQueue.AddShaderParameter(PSP_SHADOWMAPINVSIZE, currentLightParams->shadowMapInvSize_);
                    drawQueue.AddShaderParameter(PSP_SHADOWSPLITS, currentLightParams->shadowSplits_);
                    drawQueue.AddShaderParameter(PSP_SHADOWCUBEUVBIAS, currentLightParams->shadowCubeUVBias_);
                    drawQueue.AddShaderParameter(PSP_SHADOWCUBEADJUST, currentLightParams->shadowCubeAdjust_);
                    drawQueue.AddShaderParameter(VSP_NORMALOFFSETSCALE, currentLightParams->normalOffsetScale_);
                    drawQueue.AddShaderParameter(PSP_VSMSHADOWPARAMS, renderer_->GetVSMShadowParameters());
                }

                drawQueue.CommitShaderParameterGroup(SP_LIGHT);
            }
        }

        // Add resources
        if (lightDirty)
        {
            for (const auto& texture : geometryBuffer)
                drawQueue.AddShaderResource(texture.unit_, texture.texture_);

            drawQueue.AddShaderResource(TU_LIGHTRAMP, renderer_->GetDefaultLightRamp());
            drawQueue.AddShaderResource(TU_LIGHTSHAPE, renderer_->GetDefaultLightSpot());
            if (currentShadowMap)
                drawQueue.AddShaderResource(TU_SHADOWMAP, currentShadowMap);
            drawQueue.CommitShaderResources();
        }

        // Add object parameters
        if (drawQueue.BeginShaderParameterGroup(SP_OBJECT, true))
        {
            drawQueue.AddShaderParameter(VSP_MODEL, light->GetLight()->GetVolumeTransform(camera));
            drawQueue.CommitShaderParameterGroup(SP_OBJECT);
        }

        // Set buffers and draw
        drawQueue.SetBuffers(batch.geometry_->GetVertexBuffers(), batch.geometry_->GetIndexBuffer());
        drawQueue.DrawIndexed(batch.geometry_->GetIndexStart(), batch.geometry_->GetIndexCount());
    }
#endif
}

template <bool HasLight, class BatchType>
void BatchRenderer::RenderBatches(DrawCommandQueue& drawQueue, const DrawableProcessor& drawableProcessor,
    Camera* camera, Zone* zone, ea::span<const BatchType> batches)
{
    ///*
    const auto flags = HasLight
        ? BatchRenderFlag::AmbientAndVertexLights | BatchRenderFlag::PixelLight
        : BatchRenderFlag::None;
    DrawCommandCompositor compositor{ drawQueue, drawableProcessor, camera, flags };
    for (const auto& sortedBatch : batches)
        compositor.ProcessBatch(*sortedBatch.sceneBatch_);
    return;
    //*/


    const FrameInfo& frameInfo = drawableProcessor.GetFrameInfo();
    const Scene* scene = frameInfo.octree_->GetScene();
    const ea::vector<LightProcessor*>& visibleLights = drawableProcessor.GetLightProcessors();
    Node* cameraNode = camera->GetNode();

    static const LightShaderParameters defaultLightParams;
    const LightShaderParameters* currentLightParams = &defaultLightParams;
    Texture2D* currentShadowMap = nullptr;

    bool frameDirty = true;
    bool cameraDirty = true;
    float previousConstantDepthBias = 0.0f;
    const LightProcessor* previousLight = nullptr;
    LightAccumulator::VertexLightContainer previousVertexLights;
    Material* previousMaterial = nullptr;

    for (const auto& sortedBatch : batches)
    {
        const PipelineBatch& batch = *sortedBatch.sceneBatch_;
        const SourceBatch& sourceBatch = batch.GetSourceBatch();
        PipelineState* pipelineState = batch.pipelineState_;
        if (!pipelineState)
        {
            static std::atomic_flag errorReported;
            if (!errorReported.test_and_set())
                URHO3D_LOGERROR("Cannot render scene batch witout pipeline state");
            continue;
        }

        // Get used light
        const LightProcessor* light = batch.lightIndex_ != M_MAX_UNSIGNED ? visibleLights[batch.lightIndex_] : nullptr;
        const bool lightDirty = light != previousLight;
        if (lightDirty)
        {
            previousLight = light;
            currentLightParams = light ? &light->GetShaderParams() : &defaultLightParams;
            currentShadowMap = light ? light->GetShadowMap().texture_ : nullptr;
        }

        // Always set pipeline state first
        drawQueue.SetPipelineState(batch.pipelineState_);

        // Reset camera parameters if depth bias changed
        const float constantDepthBias = pipelineState->GetDesc().constantDepthBias_;

        // Add frame parameters
        if (drawQueue.BeginShaderParameterGroup(SP_FRAME, frameDirty))
        {
            AddFrameShaderParameters(drawQueue, frameInfo, scene);
            drawQueue.CommitShaderParameterGroup(SP_FRAME);
            frameDirty = false;
        }

        // Add camera parameters
        if (drawQueue.BeginShaderParameterGroup(SP_CAMERA,
            !cameraDirty || previousConstantDepthBias != constantDepthBias))
        {
            AddCameraShaderParameters(drawQueue, camera, constantDepthBias);
            drawQueue.CommitShaderParameterGroup(SP_CAMERA);
            cameraDirty = false;
            previousConstantDepthBias = constantDepthBias;
        }

        // Add light parameters
        if constexpr (HasLight)
        {
            const LightAccumulator& lighting = drawableProcessor.GetGeometryLighting(batch.drawableIndex_);
            auto vertexLights = lighting.GetVertexLights();
            ea::sort(vertexLights.begin(), vertexLights.end());
            const bool vertexLightsDirty = previousVertexLights != vertexLights;
            if (drawQueue.BeginShaderParameterGroup(SP_LIGHT, lightDirty || vertexLightsDirty))
            {
                previousVertexLights = vertexLights;

                drawQueue.AddShaderParameter(VSP_LIGHTDIR, currentLightParams->direction_);
                drawQueue.AddShaderParameter(VSP_LIGHTPOS,
                    Vector4{ currentLightParams->position_, currentLightParams->invRange_ });
                drawQueue.AddShaderParameter(PSP_LIGHTCOLOR,
                    Vector4{ currentLightParams->color_, currentLightParams->specularIntensity_ });

                drawQueue.AddShaderParameter(PSP_LIGHTRAD, currentLightParams->radius_);
                drawQueue.AddShaderParameter(PSP_LIGHTLENGTH, currentLightParams->length_);

                Vector4 vertexLightsData[MAX_VERTEX_LIGHTS * 3]{};
                for (unsigned i = 0; i < vertexLights.size(); ++i)
                {
                    if (vertexLights[i] == M_MAX_UNSIGNED)
                        continue;

                    const LightShaderParameters& vertexLightParams = visibleLights[vertexLights[i]]->GetShaderParams();
                    vertexLightsData[i * 3] = { vertexLightParams.color_, vertexLightParams.invRange_ };
                    vertexLightsData[i * 3 + 1] = { vertexLightParams.direction_, vertexLightParams.cutoff_ };
                    vertexLightsData[i * 3 + 2] = { vertexLightParams.position_, vertexLightParams.invCutoff_ };
                }
                drawQueue.AddShaderParameter(VSP_VERTEXLIGHTS, ea::span<const Vector4>{ vertexLightsData });

                if (currentLightParams->numLightMatrices_ > 0)
                {
                    ea::span<const Matrix4> shadowMatricesSpan{ currentLightParams->lightMatrices_, currentLightParams->numLightMatrices_ };
                    drawQueue.AddShaderParameter(VSP_LIGHTMATRICES, shadowMatricesSpan);
                }
                if (currentShadowMap)
                {
                    drawQueue.AddShaderParameter(PSP_SHADOWDEPTHFADE, currentLightParams->shadowDepthFade_);
                    drawQueue.AddShaderParameter(PSP_SHADOWINTENSITY, currentLightParams->shadowIntensity_);
                    drawQueue.AddShaderParameter(PSP_SHADOWMAPINVSIZE, currentLightParams->shadowMapInvSize_);
                    drawQueue.AddShaderParameter(PSP_SHADOWSPLITS, currentLightParams->shadowSplits_);
                    drawQueue.AddShaderParameter(PSP_SHADOWCUBEUVBIAS, currentLightParams->shadowCubeUVBias_);
                    drawQueue.AddShaderParameter(PSP_SHADOWCUBEADJUST, currentLightParams->shadowCubeAdjust_);
                    drawQueue.AddShaderParameter(VSP_NORMALOFFSETSCALE, currentLightParams->normalOffsetScale_);
                    drawQueue.AddShaderParameter(PSP_VSMSHADOWPARAMS, renderer_->GetVSMShadowParameters());
                }

                drawQueue.CommitShaderParameterGroup(SP_LIGHT);
            }
        }

        // Add material parameters
        const bool materialDirty = previousMaterial != batch.material_;
        if (drawQueue.BeginShaderParameterGroup(SP_MATERIAL, materialDirty))
        {
            const auto& materialParameters = batch.material_->GetShaderParameters();
            for (const auto& parameter : materialParameters)
                drawQueue.AddShaderParameter(parameter.first, parameter.second.value_);
            drawQueue.CommitShaderParameterGroup(SP_MATERIAL);
        }

        // Add resources
        if (materialDirty || lightDirty)
        {
            const auto& materialTextures = batch.material_->GetTextures();
            for (const auto& texture : materialTextures)
                drawQueue.AddShaderResource(texture.first, texture.second);

            drawQueue.AddShaderResource(TU_LIGHTRAMP, renderer_->GetDefaultLightRamp());
            drawQueue.AddShaderResource(TU_LIGHTSHAPE, renderer_->GetDefaultLightSpot());
            if (currentShadowMap)
                drawQueue.AddShaderResource(TU_SHADOWMAP, currentShadowMap);
            drawQueue.CommitShaderResources();

            previousMaterial = batch.material_;
        }

        // Add object parameters
        if (drawQueue.BeginShaderParameterGroup(SP_OBJECT, true))
        {
            const LightAccumulator& lighting = drawableProcessor.GetGeometryLighting(batch.drawableIndex_);
            const SphericalHarmonicsDot9& sh = lighting.sh_;
            drawQueue.AddShaderParameter(VSP_SHAR, sh.Ar_);
            drawQueue.AddShaderParameter(VSP_SHAG, sh.Ag_);
            drawQueue.AddShaderParameter(VSP_SHAB, sh.Ab_);
            drawQueue.AddShaderParameter(VSP_SHBR, sh.Br_);
            drawQueue.AddShaderParameter(VSP_SHBG, sh.Bg_);
            drawQueue.AddShaderParameter(VSP_SHBB, sh.Bb_);
            drawQueue.AddShaderParameter(VSP_SHC, sh.C_);
            drawQueue.AddShaderParameter(VSP_AMBIENT, Vector4(sh.EvaluateAverage(), 1.0f));
            switch (batch.geometryType_)
            {
            case GEOM_INSTANCED:
                // TODO(renderer): Implement instancing
                assert(0);
                break;
            case GEOM_SKINNED:
                drawQueue.AddShaderParameter(VSP_SKINMATRICES,
                    ea::span<const Matrix3x4>(sourceBatch.worldTransform_, sourceBatch.numWorldTransforms_));
                break;
            case GEOM_BILLBOARD:
                drawQueue.AddShaderParameter(VSP_MODEL, *sourceBatch.worldTransform_);
                if (sourceBatch.numWorldTransforms_ > 1)
                    drawQueue.AddShaderParameter(VSP_BILLBOARDROT, sourceBatch.worldTransform_[1].RotationMatrix());
                else
                    drawQueue.AddShaderParameter(VSP_BILLBOARDROT, cameraNode->GetWorldRotation().RotationMatrix());
                break;
            default:
                drawQueue.AddShaderParameter(VSP_MODEL, *sourceBatch.worldTransform_);
                break;
            }
            drawQueue.CommitShaderParameterGroup(SP_OBJECT);
        }

        // Set buffers and draw
        drawQueue.SetBuffers(sourceBatch.geometry_->GetVertexBuffers(), sourceBatch.geometry_->GetIndexBuffer());
        drawQueue.DrawIndexed(sourceBatch.geometry_->GetIndexStart(), sourceBatch.geometry_->GetIndexCount());
    }
}

}
