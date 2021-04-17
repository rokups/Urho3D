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

void RenderPipelineSettings::AdjustToSupported(Context* context)
{
    auto graphics = context->GetSubsystem<Graphics>();

    // Validate render target setup
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

    shadowMapAllocator_.shadowAtlasPageSize_ = ea::min(
        shadowMapAllocator_.shadowAtlasPageSize_, Graphics::GetCaps().maxRenderTargetSize_);

    if (sceneProcessor_.IsDeferredLighting() && !graphics->GetDeferredSupport()
        && !Graphics::GetReadableDepthStencilFormat())
        sceneProcessor_.lightingMode_ = DirectLightingMode::Forward;

    if (!shadowMapAllocator_.use16bitShadowMaps_ && !graphics->GetHiresShadowMapFormat())
        shadowMapAllocator_.use16bitShadowMaps_ = true;

    if (instancingBuffer_.enableInstancing_ && !graphics->GetInstancingSupport())
        instancingBuffer_.enableInstancing_ = false;

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

    sceneProcessor_.linearSpaceLighting_ =
        renderBufferManager_.colorSpace_ != RenderPipelineColorSpace::GammaLDR;

    // Validate post-processing
#ifdef GL_ES_VERSION_2_0
    if (antialiasing_ == PostProcessAntialiasing::FXAA3)
    {
        URHO3D_LOGWARNING("FXAA3 is not supported, falling back to FXAA2");
        antialiasing_ = PostProcessAntialiasing::FXAA2;
    }
#endif

    bloom_.hdr_ = renderBufferManager_.colorSpace_ == RenderPipelineColorSpace::LinearHDR;
}

}
