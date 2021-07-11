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

#pragma once

#include "../RenderPipeline/RenderPipelineDefs.h"
#include "../RenderPipeline/PipelineBatchSortKey.h"

namespace Urho3D
{

class BatchCompositor;
class BatchRenderer;
class CameraProcessor;
class Drawable;
class DrawableProcessor;
class DrawCommandQueue;
class InstancingBuffer;
class PipelineStateBuilder;
class RenderPipelineInterface;
class RenderSurface;
class ScenePass;
class ShadowMapAllocator;
class Viewport;
struct ShaderParameterDesc;
struct ShaderResourceDesc;

/// Scene processor for RenderPipeline
class URHO3D_API SceneProcessor : public Object, public LightProcessorCallback
{
    URHO3D_OBJECT(SceneProcessor, Object);

public:
    SceneProcessor(RenderPipelineInterface* renderPipeline, const ea::string& shadowTechnique,
        ShadowMapAllocator* shadowMapAllocator, InstancingBuffer* instancingBuffer);
    ~SceneProcessor() override;

    /// Setup. Slow, called rarely.
    /// @{
    /// It's okay to pass null passes in vector.
    void SetPasses(ea::vector<SharedPtr<ScenePass>> passes);
    void SetSettings(const ShaderProgramCompositorSettings& settings);
    /// @}

    /// Create scene passes
    /// @{
    template <class T> SharedPtr<T> CreatePass(DrawableProcessorPassFlags flags, const ea::string& deferredPass,
        const ea::string& unlitBasePass, const ea::string& litBasePass, const ea::string& lightPass)
    {
        return MakeShared<T>(renderPipeline_, drawableProcessor_, batchStateCacheCallback_,
            flags, deferredPass, unlitBasePass, litBasePass, lightPass);
    }
    template <class T> SharedPtr<T> CreatePass(DrawableProcessorPassFlags flags, const ea::string& pass)
    {
        return MakeShared<T>(renderPipeline_, drawableProcessor_, batchStateCacheCallback_, flags, pass);
    }
    /// @}

    /// Called on every frame.
    /// @{
    bool Define(const CommonFrameInfo& frameInfo);
    void SetRenderCamera(Camera* camera);
    void SetRenderCameras(ea::span<Camera* const> cameras);

    void Update();
    void PrepareInstancingBuffer();
    void RenderShadowMaps();
    void RenderSceneBatches(ea::string_view debugName, Camera* camera,
        const PipelineBatchGroup<PipelineBatchByState>& batchGroup,
        ea::span<const ShaderResourceDesc> globalResources = {}, ea::span<const ShaderParameterDesc> cameraParameters = {});
    void RenderSceneBatches(ea::string_view debugName, Camera* camera,
        const PipelineBatchGroup<PipelineBatchBackToFront>& batchGroup,
        ea::span<const ShaderResourceDesc> globalResources = {}, ea::span<const ShaderParameterDesc> cameraParameters = {});
    void RenderLightVolumeBatches(ea::string_view debugName, Camera* camera,
        ea::span<const ShaderResourceDesc> globalResources, ea::span<const ShaderParameterDesc> cameraParameters);
    /// @}

    /// Getters
    /// @{
    const SceneProcessorSettings& GetSettings() const { return settings_; }
    const FrameInfo& GetFrameInfo() const { return frameInfo_; }
    /// Takes pass object from BatchStateCreateContext.
    /// Returns user-configured pass (inherited from BatchCompositorPass) if possible.
    /// Returns nullptr if internal pass from BatchCompositor itself (e.g. shadow or light volume pass).
    BatchCompositorPass* GetUserPass(Object* pass) const;
    const auto& GetLightVolumeBatches() const { return batchCompositor_->GetLightVolumeBatches(); }
    CameraProcessor* GetCameraProcessor() const { return cameraProcessor_; }
    PipelineStateBuilder* GetPipelineStateBuilder() const { return pipelineStateBuilder_; }
    DrawableProcessor* GetDrawableProcessor() const { return drawableProcessor_; }
    BatchCompositor* GetBatchCompositor() const { return batchCompositor_; }
    BatchRenderer* GetBatchRenderer() const { return batchRenderer_; }
    /// @}

private:
    /// Callbacks from RenderPipeline
    /// @{
    void OnUpdateBegin(const CommonFrameInfo& frameInfo);
    void OnRenderBegin(const CommonFrameInfo& frameInfo);
    void OnRenderEnd(const CommonFrameInfo& frameInfo);
    /// @}

    /// LightProcessorCallback implementation
    /// @{
    bool IsLightShadowed(Light* light) override;
    unsigned GetShadowMapSize(Light* light, unsigned numActiveSplits) const override;
    ShadowMapRegion AllocateTransientShadowMap(const IntVector2& size) override;
    /// @}

    void DrawOccluders();
    template <class T>
    void RenderBatchesInternal(ea::string_view debugName, Camera* camera, const PipelineBatchGroup<T>& batchGroup,
        ea::span<const ShaderResourceDesc> globalResources, ea::span<const ShaderParameterDesc> cameraParameters);

    Graphics* graphics_{};
    RenderPipelineInterface* renderPipeline_{};
    RenderPipelineDebugger* debugger_{};

    /// Shared objects
    /// @{
    SharedPtr<ShadowMapAllocator> shadowMapAllocator_;
    SharedPtr<InstancingBuffer> instancingBuffer_;
    SharedPtr<DrawCommandQueue> drawQueue_;
    /// @}

    /// Owned objects
    /// @{
    SharedPtr<CameraProcessor> cameraProcessor_;
    SharedPtr<PipelineStateBuilder> pipelineStateBuilder_;
    SharedPtr<DrawableProcessor> drawableProcessor_;
    SharedPtr<BatchCompositor> batchCompositor_;
    SharedPtr<BatchRenderer> batchRenderer_;
    SharedPtr<OcclusionBuffer> occlusionBuffer_;
    BatchStateCacheCallback* batchStateCacheCallback_{};
    /// @}

    SceneProcessorSettings settings_;
    ea::vector<SharedPtr<ScenePass>> passes_;

    FrameInfo frameInfo_;
    bool flipCameraForRendering_{};

    OcclusionBuffer* currentOcclusionBuffer_{};
    ea::vector<Drawable*> occluders_;
    ea::vector<Drawable*> drawables_;
};

}
