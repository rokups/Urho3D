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
#include "../Graphics/Graphics.h"
#include "../Graphics/GraphicsImpl.h"
#include "../RenderPipeline/RenderPipelineDefs.h"

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

int GetClosestMutliSampleLevel(Graphics* graphics, int level)
{
    const auto& supportedLevels = graphics->GetMultiSampleLevels();
    const auto iter = ea::upper_bound(supportedLevels.begin(), supportedLevels.end(), level);
    return iter != supportedLevels.begin() ? *ea::prev(iter) : 1;
}

}

void RenderPipelineSettings::AdjustToSupported(Context* context)
{
    auto graphics = context->GetSubsystem<Graphics>();
    const GraphicsCaps& caps = Graphics::GetCaps();

    // RenderBufferManagerSettings
    renderBufferManager_.multiSampleLevel_ = GetClosestMutliSampleLevel(graphics, renderBufferManager_.multiSampleLevel_);

    if (renderBufferManager_.colorSpace_ == RenderPipelineColorSpace::LinearHDR
        && graphics->GetRGBAFloat16Format() == graphics->GetRGBAFormat())
    {
        URHO3D_LOGWARNING("HDR rendering is not supported, falling back to LDR");
        renderBufferManager_.colorSpace_ = RenderPipelineColorSpace::LinearLDR;
    }

    if (renderBufferManager_.colorSpace_ == RenderPipelineColorSpace::LinearLDR
        && !graphics->GetSRGBWriteSupport())
    {
        URHO3D_LOGWARNING("sRGB render targets are not supported, falling back to gamma color space");
        renderBufferManager_.colorSpace_ = RenderPipelineColorSpace::GammaLDR;
    }

#ifdef GL_ES_VERSION_2_0
    renderBufferManager_.readableDepth_ = false;
#endif

    // OcclusionBufferSettings

    // BatchRendererSettings

    // SceneProcessorSettings
    if (!graphics->GetShadowMapFormat() && !graphics->GetHiresShadowMapFormat())
        sceneProcessor_.enableShadows_ = false;

#ifdef GL_ES_VERSION_2_0
    const bool deferredSupported = false;
#else
    const bool deferredSupported = caps.maxNumRenderTargets_ >= 4 && Graphics::GetReadableDepthStencilFormat();
#endif
    if (sceneProcessor_.IsDeferredLighting() && !deferredSupported)
        sceneProcessor_.lightingMode_ = DirectLightingMode::Forward;

    // ShadowMapAllocatorSettings
    if (!graphics->GetRGFloat32Format())
        shadowMapAllocator_.enableVarianceShadowMaps_ = false;

    shadowMapAllocator_.varianceShadowMapMultiSample_ = GetClosestMutliSampleLevel(
        graphics, shadowMapAllocator_.varianceShadowMapMultiSample_);

    if (!graphics->GetHiresShadowMapFormat())
        shadowMapAllocator_.use16bitShadowMaps_ = true;

    shadowMapAllocator_.shadowAtlasPageSize_ = ea::min(
        shadowMapAllocator_.shadowAtlasPageSize_, caps.maxRenderTargetSize_);

    // InstancingBufferSettings
    if (!graphics->GetInstancingSupport())
        instancingBuffer_.enableInstancing_ = false;

    // TODO: Check if instancing is actually supported, i.e. if there's enough vertex attributes

    // RenderPipelineSettings
#ifdef GL_ES_VERSION_2_0
    if (antialiasing_ == PostProcessAntialiasing::FXAA3)
    {
        URHO3D_LOGWARNING("FXAA3 is not supported, falling back to FXAA2");
        antialiasing_ = PostProcessAntialiasing::FXAA2;
    }
#endif
}

void RenderPipelineSettings::PropagateImpliedSettings()
{
    // Deferred rendering expects certain properties from render textures
    if (sceneProcessor_.IsDeferredLighting())
    {
        renderBufferManager_.colorUsableWithMultipleRenderTargets_ = true;
        renderBufferManager_.stencilBuffer_ = true;
        renderBufferManager_.readableDepth_ = true;
        renderBufferManager_.inheritMultiSampleLevel_ = false;
    }

    // Setup instancing buffer format
    if (instancingBuffer_.enableInstancing_)
    {
        instancingBuffer_.firstInstancingTexCoord_ = 4;
        switch (sceneProcessor_.ambientMode_)
        {
        case DrawableAmbientMode::Constant:
            instancingBuffer_.numInstancingTexCoords_ = 3;
            break;
        case DrawableAmbientMode::Flat:
            instancingBuffer_.numInstancingTexCoords_ = 3 + 1;
            break;
        case DrawableAmbientMode::Directional:
            instancingBuffer_.numInstancingTexCoords_ = 3 + 7;
            break;
        }
    }

    // Synchronize misc settings
    sceneProcessor_.linearSpaceLighting_ =
        renderBufferManager_.colorSpace_ != RenderPipelineColorSpace::GammaLDR;

    bloom_.hdr_ = renderBufferManager_.colorSpace_ == RenderPipelineColorSpace::LinearHDR;
}

void RenderPipelineSettings::AdjustForPostProcessing(PostProcessPassFlags flags)
{
    renderBufferManager_.filteredColor_ = flags.Test(PostProcessPassFlag::NeedColorOutputBilinear);
}

}
