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
#include "../Graphics/Graphics.h"
#include "../Graphics/Renderer.h"
#include "../RenderPipeline/ShadowMapAllocator.h"

#include "../DebugNew.h"

namespace Urho3D
{

ShadowMapRegion ShadowMapRegion::GetSplit(unsigned split, const IntVector2& numSplits) const
{
    const IntVector2 splitSize = rect_.Size() / numSplits;
    assert(rect_.Size() == splitSize * numSplits);

    const IntVector2 index{ static_cast<int>(split % numSplits.x_), static_cast<int>(split / numSplits.x_) };
    const IntVector2 splitBegin = rect_.Min() + splitSize * index;
    const IntVector2 splitEnd = splitBegin + splitSize;

    ShadowMapRegion splitShadowMap = *this;
    splitShadowMap.rect_ = { splitBegin, splitEnd };
    return splitShadowMap;
}

ShadowMapAllocator::ShadowMapAllocator(Context* context)
    : Object(context)
    , graphics_(context_->GetSubsystem<Graphics>())
    , renderer_(context_->GetSubsystem<Renderer>())
{
    CacheSettings();
}

void ShadowMapAllocator::SetSettings(const ShadowMapAllocatorSettings& settings)
{
    if (settings_ != settings)
    {
        settings_ = settings;
        CacheSettings();

        dummyColorTexture_ = nullptr;
        pages_.clear();
    }
}

void ShadowMapAllocator::CacheSettings()
{
    if (settings_.enableVarianceShadowMaps_)
        shadowMapFormat_ = graphics_->GetRGFloat32Format();
    else
    {
        shadowMapFormat_ = settings_.use16bitShadowMaps_
            ? graphics_->GetShadowMapFormat()
            : graphics_->GetHiresShadowMapFormat();
    }

    shadowAtlasPageSize_ = static_cast<int>(settings_.shadowAtlasPageSize_) * IntVector2::ONE;
}

void ShadowMapAllocator::ResetAllShadowMaps()
{
    for (AtlasPage& element : pages_)
    {
        element.areaAllocator_.Reset(shadowAtlasPageSize_.x_, shadowAtlasPageSize_.y_, shadowAtlasPageSize_.x_, shadowAtlasPageSize_.y_);
        element.clearBeforeRendering_ = false;
    }
}

ShadowMapRegion ShadowMapAllocator::AllocateShadowMap(const IntVector2& size)
{
    if (!settings_.shadowAtlasPageSize_ || !shadowMapFormat_)
        return {};

    const IntVector2 clampedSize = VectorMin(size, shadowAtlasPageSize_);

    for (AtlasPage& element : pages_)
    {
        const ShadowMapRegion shadowMap = element.AllocateRegion(clampedSize);
        if (shadowMap)
            return shadowMap;
    }

    AllocatePage();
    return pages_.back().AllocateRegion(clampedSize);
}

bool ShadowMapAllocator::BeginShadowMapRendering(const ShadowMapRegion& shadowMap)
{
    if (!shadowMap || shadowMap.pageIndex_ >= pages_.size())
        return false;

    graphics_->SetTexture(TU_SHADOWMAP, nullptr);

    Texture2D* shadowMapTexture = shadowMap.texture_;
    AtlasPage& poolElement = pages_[shadowMap.pageIndex_];

    if (shadowMapTexture->GetUsage() == TEXTURE_DEPTHSTENCIL)
    {
        // The shadow map is a depth stencil texture
        graphics_->SetDepthStencil(shadowMapTexture);
        graphics_->SetRenderTarget(0, shadowMapTexture->GetRenderSurface()->GetLinkedRenderTarget());
    }
    else
    {
        // The shadow map is a color rendertarget
        graphics_->SetDepthStencil(renderer_->GetDepthStencil(shadowMapTexture->GetWidth(), shadowMapTexture->GetHeight(),
            shadowMapTexture->GetMultiSample(), shadowMapTexture->GetAutoResolve()));
        graphics_->SetRenderTarget(0, shadowMapTexture);
    }

    // Disable other render targets
    for (unsigned i = 1; i < MAX_RENDERTARGETS; ++i)
        graphics_->SetRenderTarget(i, (RenderSurface*) nullptr);

    // Clear whole texture if needed
    if (poolElement.clearBeforeRendering_)
    {
        poolElement.clearBeforeRendering_ = false;

        graphics_->SetViewport(shadowMapTexture->GetRect());
        ClearTargetFlags clearFlags = CLEAR_DEPTH;
        if (settings_.enableVarianceShadowMaps_ || dummyColorTexture_)
            clearFlags |= CLEAR_COLOR;
        graphics_->Clear(clearFlags, Color::WHITE);
    }

    graphics_->SetViewport(shadowMap.rect_);

    return true;
}

ShadowMapRegion ShadowMapAllocator::AtlasPage::AllocateRegion(const IntVector2& size)
{
    int x{}, y{};
    if (areaAllocator_.Allocate(size.x_, size.y_, x, y))
    {
        const IntVector2 offset{ x, y };
        ShadowMapRegion shadowMap;
        shadowMap.pageIndex_ = index_;
        shadowMap.texture_ = texture_;
        shadowMap.rect_ = IntRect(offset, offset + size);

        // Mark shadow map as used
        clearBeforeRendering_ = true;
        return shadowMap;
    }
    return {};
}

void ShadowMapAllocator::AllocatePage()
{
    const bool isDepthTexture = !settings_.enableVarianceShadowMaps_;
    const TextureUsage textureUsage = isDepthTexture ? TEXTURE_DEPTHSTENCIL : TEXTURE_RENDERTARGET;
    const int multiSample = isDepthTexture ? 1 : settings_.varianceShadowMapMultiSample_;

    auto newShadowMap = MakeShared<Texture2D>(context_);
    const unsigned dummyColorFormat = graphics_->GetDummyColorFormat();

    // Disable mipmaps from the shadow map
    newShadowMap->SetNumLevels(1);
    newShadowMap->SetSize(shadowAtlasPageSize_.x_, shadowAtlasPageSize_.y_, shadowMapFormat_, textureUsage, multiSample);

#ifndef GL_ES_VERSION_2_0
    // OpenGL (desktop) and D3D11: shadow compare mode needs to be specifically enabled for the shadow map
    newShadowMap->SetFilterMode(FILTER_BILINEAR);
    newShadowMap->SetShadowCompare(isDepthTexture);
#endif
#ifndef URHO3D_OPENGL
    // Direct3D9: when shadow compare must be done manually, use nearest filtering so that the filtering of point lights
    // and other shadowed lights matches
    newShadowMap->SetFilterMode(graphics_->GetHardwareShadowSupport() ? FILTER_BILINEAR : FILTER_NEAREST);
#endif
    // Create dummy color texture for the shadow map if necessary: Direct3D9, or OpenGL when working around an OS X +
    // Intel driver bug
    if (isDepthTexture && dummyColorFormat)
    {
        // If no dummy color rendertarget for this size exists yet, create one now
        if (!dummyColorTexture_)
        {
            dummyColorTexture_ = MakeShared<Texture2D>(context_);
            dummyColorTexture_->SetNumLevels(1);
            dummyColorTexture_->SetSize(shadowAtlasPageSize_.x_, shadowAtlasPageSize_.y_,
                dummyColorFormat, TEXTURE_RENDERTARGET);
        }
        // Link the color rendertarget to the shadow map
        newShadowMap->GetRenderSurface()->SetLinkedRenderTarget(dummyColorTexture_->GetRenderSurface());
    }

    // Store allocate shadow map
    AtlasPage& element = pages_.emplace_back();
    element.index_ = pages_.size() - 1;
    element.texture_ = newShadowMap;
    element.areaAllocator_.Reset(shadowAtlasPageSize_.x_, shadowAtlasPageSize_.y_, shadowAtlasPageSize_.x_, shadowAtlasPageSize_.y_);
}

}
