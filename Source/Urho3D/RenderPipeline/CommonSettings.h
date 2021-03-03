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

#include "../Container/Hash.h"
#include "../Graphics/GraphicsDefs.h"
#include "../Math/Vector2.h"

namespace Urho3D
{

/// Drawable processor settings.
struct DrawableProcessorSettings
{
    /// Material quality.
    MaterialQuality materialQuality_{ QUALITY_HIGH };
    /// Max number of vertex lights.
    unsigned maxVertexLights_{ 4 };
    /// Max number of pixel lights.
    unsigned maxPixelLights_{ 4 };

    /// Calculate pipeline state hash.
    unsigned CalculatePipelineStateHash() const { return 0; }

    /// Compare settings.
    bool operator==(const DrawableProcessorSettings& rhs) const
    {
        return materialQuality_ == rhs.materialQuality_
            && maxVertexLights_ == rhs.maxVertexLights_
            && maxPixelLights_ == rhs.maxPixelLights_;
    }

    /// Compare settings.
    bool operator!=(const DrawableProcessorSettings& rhs) const { return !(*this == rhs); }
};

/// Instancing buffer compositor settings.
struct InstancingBufferSettings
{
    /// Whether to enable instanching.
    // TODO(renderer): Make true when implemented
    bool enable_{ false };
    /// First UV element that can be used by instancing.
    unsigned firstUnusedTexCoord_{};
    /// Number of elements reserved for internal usage.
    unsigned numReservedElems_{};

    /// Calculate pipeline state hash.
    unsigned CalculatePipelineStateHash() const
    {
        unsigned hash = 0;
        CombineHash(hash, enable_);
        CombineHash(hash, firstUnusedTexCoord_);
        CombineHash(hash, numReservedElems_);
        return hash;
    }

    /// Compare settings.
    bool operator==(const InstancingBufferSettings& rhs) const
    {
        return enable_ == rhs.enable_
            && firstUnusedTexCoord_ == rhs.firstUnusedTexCoord_
            && numReservedElems_ == rhs.numReservedElems_;
    }

    /// Compare settings.
    bool operator!=(const InstancingBufferSettings& rhs) const { return !(*this == rhs); }
};

/// Ambient lighting mode.
enum class AmbientMode
{
    Constant,
    Flat,
    Directional,
};

/// Batch renderer settings.
struct BatchRendererSettings
{
    /// Whether to apply gamma correction.
    bool gammaCorrection_{};

    /// Ambient mode.
    AmbientMode ambientMode_{ AmbientMode::Directional };
    /// Variance shadow map parameters.
    Vector2 vsmShadowParams_{ 0.0000001f, 0.9f };

    /// Calculate pipeline state hash.
    unsigned CalculatePipelineStateHash() const
    {
        unsigned hash = 0;
        CombineHash(hash, gammaCorrection_);
        CombineHash(hash, MakeHash(ambientMode_));
        return hash;
    }

    /// Compare settings.
    bool operator==(const BatchRendererSettings& rhs) const
    {
        return gammaCorrection_ == rhs.gammaCorrection_
            && ambientMode_ == rhs.ambientMode_
            && vsmShadowParams_ == rhs.vsmShadowParams_;
    }

    /// Compare settings.
    bool operator!=(const BatchRendererSettings& rhs) const { return !(*this == rhs); }
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

    /// Calculate pipeline state hash.
    unsigned CalculatePipelineStateHash() const
    {
        unsigned hash = 0;
        CombineHash(hash, varianceShadowMap_);
        CombineHash(hash, lowPrecisionShadowMaps_);
        return hash;
    }

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

/// Scene processor settings.
struct SceneProcessorSettings
{
    /// Default VSM shadow params.
    static const Vector2 DefaultVSMShadowParams;

    /// Drawable processor settings.
    DrawableProcessorSettings drawableProcessing_;
    /// Shadow map allocator settings.
    ShadowMapAllocatorSettings shadowMap_;
    /// Instancing buffer settings.
    InstancingBufferSettings instancing_;
    /// Batch rendering settings.
    BatchRendererSettings rendering_;

    /// Whether to render shadows.
    bool enableShadows_{ true };
    /// Whether to enable deferred rendering.
    bool deferred_{ false };

    /// Whether to render occlusion triangles in multiple threads.
    bool threadedOcclusion_{};
    /// Max number of occluder triangles.
    unsigned maxOccluderTriangles_{ 5000 };
    /// Occlusion buffer width.
    unsigned occlusionBufferSize_{ 256 };
    /// Occluder screen size threshold.
    float occluderSizeThreshold_{ 0.025f };

    /// Calculate pipeline state hash.
    unsigned CalculatePipelineStateHash() const
    {
        unsigned hash = 0;
        CombineHash(hash, drawableProcessing_.CalculatePipelineStateHash());
        CombineHash(hash, shadowMap_.CalculatePipelineStateHash());
        CombineHash(hash, instancing_.CalculatePipelineStateHash());
        CombineHash(hash, rendering_.CalculatePipelineStateHash());
        CombineHash(hash, enableShadows_);
        CombineHash(hash, deferred_);
        return hash;
    }

    /// Compare settings.
    bool operator==(const SceneProcessorSettings& rhs) const
    {
        return drawableProcessing_ == rhs.drawableProcessing_
            && shadowMap_ == rhs.shadowMap_
            && instancing_ == rhs.instancing_
            && rendering_ == rhs.rendering_
            && enableShadows_ == rhs.enableShadows_
            && deferred_ == rhs.deferred_
            && threadedOcclusion_ == rhs.threadedOcclusion_
            && maxOccluderTriangles_ == rhs.maxOccluderTriangles_
            && occlusionBufferSize_ == rhs.occlusionBufferSize_
            && occluderSizeThreshold_ == rhs.occluderSizeThreshold_;
    }

    /// Compare settings.
    bool operator!=(const SceneProcessorSettings& rhs) const { return !(*this == rhs); }
};

}
