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

#include "../Core/Object.h"
#include "../Math/AreaAllocator.h"
#include "../Math/Rect.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/Light.h"
#include "../RenderPipeline/RenderPipelineDefs.h"

#include <EASTL/vector.h>

namespace Urho3D
{

class Renderer;

/// Utility to allocate shadow maps in texture atlas.
class URHO3D_API ShadowMapAllocator : public Object
{
    URHO3D_OBJECT(ShadowMapAllocator, Object);

public:
    explicit ShadowMapAllocator(Context* context);
    void SetSettings(const ShadowMapAllocatorSettings& settings);

    /// Reset allocated shadow maps.
    void ResetAllShadowMaps();
    /// Allocate shadow map of given size. It is better to allocate from bigger to smaller sizes.
    ShadowMapRegion AllocateShadowMap(const IntVector2& size);
    /// Begin shadow map rendering. Clears shadow map if necessary.
    bool BeginShadowMapRendering(const ShadowMapRegion& shadowMap);

    const ShadowMapAllocatorSettings& GetSettings() const { return settings_; }

private:
    struct AtlasPage
    {
        unsigned index_{};
        SharedPtr<Texture2D> texture_;
        AreaAllocator areaAllocator_;
        bool clearBeforeRendering_{};

        /// Allocate shadow map.
        ShadowMapRegion AllocateRegion(const IntVector2& size);
    };

    void CacheSettings();
    void AllocatePage();

    /// External dependencies
    /// @{
    Graphics* graphics_{};
    Renderer* renderer_{};
    /// @}

    /// Settings
    /// @{
    ShadowMapAllocatorSettings settings_;
    unsigned shadowMapFormat_{};
    IntVector2 shadowAtlasPageSize_;
    /// @}

    /// Dummy color map for workaround, if needed.
    SharedPtr<Texture2D> dummyColorTexture_;
    ea::vector<AtlasPage> pages_;
};

}
