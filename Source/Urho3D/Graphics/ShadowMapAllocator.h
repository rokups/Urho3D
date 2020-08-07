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

/// Shadow map type.
enum class ShadowMapType
{
    /// 16-bit depth texture.
    Depth16,
    /// 24-bit depth texture.
    Depth24,
    /// Depth and variance in 32-bit RG float color texture.
    ColorRG32
};

/// Utility to allocate shadow maps in texture pool.
// TODO(renderer): Track pipeline state changes caused by renderer settings
class ShadowMapAllocator : public Object
{
    URHO3D_OBJECT(ShadowMapAllocator, Object);

public:
    /// Construct.
    explicit ShadowMapAllocator(Context* context);
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

    /// Return shadow map type from renderer.
    ShadowMapType GetShadowMapType() const;
    /// Allocate one more texture.
    void AllocateNewTexture();

    /// Graphics subsystem.
    Graphics* graphics_{};
    /// Renderer subsystem.
    Renderer* renderer_{};

    /// Dummy color map, if needed.
    SharedPtr<Texture2D> dummyColorTexture_;
    /// Texture pool.
    ea::vector<PoolElement> pool_;
    /// Size of texture.
    int shadowMapSize_{};
    /// Internal texture type.
    ShadowMapType shadowMapType_{};
    /// VSM multisample level.
    int vsmMultiSample_{};

    /// Shadow map texture format.
    unsigned shadowMapFormat_{};
    /// Shadow map texture usage.
    TextureUsage shadowMapUsage_{};
    /// Shadow map multisample level.
    int multiSample_{};
};

}
