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
#include "../Graphics/PipelineState.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/Light.h"

#include <EASTL/vector.h>

namespace Urho3D
{

class Renderer;

/// Allocated shadow map.
struct ShadowMap
{
    /// Index of texture in the pool.
    unsigned index_{};
    /// Underlying texture.
    SharedPtr<Texture2D> texture_;
    /// Region in texture.
    IntRect region_;

    /// Return whether the shadow map is valid.
    operator bool() const { return !!texture_; }
    /// Return shadow map split.
    ShadowMap GetSplit(unsigned split, const IntVector2& numSplits) const;
};

/// Shadow map allocator settings.
struct ShadowMapAllocatorSettings
{
    /// Whether to use Variance Shadow Maps
    bool varianceShadowMap_{};
    /// Multisampling level of Variance Shadow Maps.
    int varianceShadowMapMultiSample_{ 1 };
    /// Whether to use low precision 16-bit depth maps.
    bool lowPrecisionShadowMaps_{};
    /// Size of shadow map atlas page.
    unsigned shadowMapPageSize_{ 2048 };

    /// Compare settings.
    bool operator==(const ShadowMapAllocatorSettings& rhs) const
    {
        return varianceShadowMap_ == rhs.varianceShadowMap_
            && varianceShadowMapMultiSample_ == rhs.varianceShadowMapMultiSample_
            && lowPrecisionShadowMaps_ == rhs.lowPrecisionShadowMaps_
            && shadowMapPageSize_ == rhs.shadowMapPageSize_;
    }

    /// Compare settings.
    bool operator!=(const ShadowMapAllocatorSettings& rhs) const { return !(*this == rhs); }
};

/// Utility to allocate shadow maps in texture pool.
// TODO(renderer): Track pipeline state changes caused by renderer settings
class URHO3D_API ShadowMapAllocator : public Object
{
    URHO3D_OBJECT(ShadowMapAllocator, Object);

public:
    /// Construct.
    explicit ShadowMapAllocator(Context* context);
    /// Set settings.
    void SetSettings(const ShadowMapAllocatorSettings& settings);

    /// Reset allocated shadow maps.
    void Reset();
    /// Allocate shadow map of given size. It is better to allocate from bigger to smaller sizes.
    ShadowMap AllocateShadowMap(const IntVector2& size);
    /// Begin shadow map rendering. Clears shadow map if necessary.
    bool BeginShadowMap(const ShadowMap& shadowMap);
    /// Export relevant parts of pipeline state. Thread safe unless called simultaneously with Reset.
    void ExportPipelineState(PipelineStateDesc& desc, const BiasParameters& biasParameters);

private:
    /// Element of texture pool.
    struct PoolElement
    {
        /// Index of pool element.
        unsigned index_{};
        /// Underlying texture.
        SharedPtr<Texture2D> texture_;
        /// Area allocator.
        AreaAllocator allocator_;
        /// Whether the texture has to be cleared.
        bool needClear_{};

        /// Allocate shadow map.
        ShadowMap Allocate(const IntVector2& size);
    };

    /// Process and cache settings.
    void CacheSettings();
    /// Allocate one more texture.
    void AllocateNewTexture();

    /// Graphics subsystem.
    Graphics* graphics_{};
    /// Renderer subsystem.
    Renderer* renderer_{};

    /// Settings.
    ShadowMapAllocatorSettings settings_;
    /// Shadow map texture format.
    unsigned shadowMapFormat_{};
    /// Shadow map texture size.
    IntVector2 shadowMapPageSize_;

    /// Dummy color map, if needed.
    SharedPtr<Texture2D> dummyColorTexture_;
    /// Texture pool.
    ea::vector<PoolElement> pool_;
};

}
