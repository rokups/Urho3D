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

struct DrawableProcessorSettings
{
    MaterialQuality materialQuality_{ QUALITY_HIGH };
    // TODO(renderer): Make maxVertexLights_ contribute to pipeline state
    unsigned maxVertexLights_{ 4 };
    unsigned maxPixelLights_{ 4 };

    /// Utility operators
    /// @{
    unsigned CalculatePipelineStateHash() const
    {
        return 0;
    }

    bool operator==(const DrawableProcessorSettings& rhs) const
    {
        return materialQuality_ == rhs.materialQuality_
            && maxVertexLights_ == rhs.maxVertexLights_
            && maxPixelLights_ == rhs.maxPixelLights_;
    }

    bool operator!=(const DrawableProcessorSettings& rhs) const { return !(*this == rhs); }
    /// @}
};

struct InstancingBufferSettings
{
    // TODO(renderer): Make true when implemented
    bool enableInstancing_{ false };
    unsigned firstInstancingTexCoord_{};
    unsigned numInstancingTexCoords_{};

    /// Utility operators
    /// @{
    unsigned CalculatePipelineStateHash() const
    {
        unsigned hash = 0;
        CombineHash(hash, enableInstancing_);
        CombineHash(hash, firstInstancingTexCoord_);
        CombineHash(hash, numInstancingTexCoords_);
        return hash;
    }

    bool operator==(const InstancingBufferSettings& rhs) const
    {
        return enableInstancing_ == rhs.enableInstancing_
            && firstInstancingTexCoord_ == rhs.firstInstancingTexCoord_
            && numInstancingTexCoords_ == rhs.numInstancingTexCoords_;
    }

    bool operator!=(const InstancingBufferSettings& rhs) const { return !(*this == rhs); }
    /// @}
};

enum class DrawableAmbientMode
{
    Constant,
    Flat,
    Directional,
};

struct BatchRendererSettings
{
    bool gammaCorrection_{};
    DrawableAmbientMode ambientMode_{ DrawableAmbientMode::Directional };
    Vector2 varianceShadowMapParams_{ 0.0000001f, 0.9f };

    /// Utility operators
    /// @{
    unsigned CalculatePipelineStateHash() const
    {
        unsigned hash = 0;
        CombineHash(hash, gammaCorrection_);
        CombineHash(hash, MakeHash(ambientMode_));
        return hash;
    }

    bool operator==(const BatchRendererSettings& rhs) const
    {
        return gammaCorrection_ == rhs.gammaCorrection_
            && ambientMode_ == rhs.ambientMode_
            && varianceShadowMapParams_ == rhs.varianceShadowMapParams_;
    }

    bool operator!=(const BatchRendererSettings& rhs) const { return !(*this == rhs); }
    /// @}
};

struct ShadowMapAllocatorSettings
{
    bool enableVarianceShadowMaps_{};
    int varianceShadowMapMultiSample_{ 1 };
    bool use16bitShadowMaps_{};
    unsigned shadowAtlasPageSize_{ 2048 };

    /// Utility operators
    /// @{
    unsigned CalculatePipelineStateHash() const
    {
        unsigned hash = 0;
        CombineHash(hash, enableVarianceShadowMaps_);
        CombineHash(hash, use16bitShadowMaps_);
        return hash;
    }

    bool operator==(const ShadowMapAllocatorSettings& rhs) const
    {
        return enableVarianceShadowMaps_ == rhs.enableVarianceShadowMaps_
            && varianceShadowMapMultiSample_ == rhs.varianceShadowMapMultiSample_
            && use16bitShadowMaps_ == rhs.use16bitShadowMaps_
            && shadowAtlasPageSize_ == rhs.shadowAtlasPageSize_;
    }

    bool operator!=(const ShadowMapAllocatorSettings& rhs) const { return !(*this == rhs); }
    /// @}
};

struct OcclusionBufferSettings
{
    bool threadedOcclusion_{};
    unsigned maxOccluderTriangles_{ 5000 };
    unsigned occlusionBufferSize_{ 256 };
    float occluderSizeThreshold_{ 0.025f };

    /// Utility operators
    /// @{
    unsigned CalculatePipelineStateHash() const
    {
        return 0;
    }

    bool operator==(const OcclusionBufferSettings& rhs) const
    {
        return threadedOcclusion_ == rhs.threadedOcclusion_
            && maxOccluderTriangles_ == rhs.maxOccluderTriangles_
            && occlusionBufferSize_ == rhs.occlusionBufferSize_
            && occluderSizeThreshold_ == rhs.occluderSizeThreshold_;
    }

    bool operator!=(const OcclusionBufferSettings& rhs) const { return !(*this == rhs); }
    /// @}
};

struct SceneProcessorSettings
    : public DrawableProcessorSettings
    , public OcclusionBufferSettings
    , public ShadowMapAllocatorSettings
    , public InstancingBufferSettings
    , public BatchRendererSettings
{
    bool enableShadows_{ true };
    bool deferredLighting_{ false };

    /// Utility operators
    /// @{
    unsigned CalculatePipelineStateHash() const
    {
        unsigned hash = 0;
        CombineHash(hash, DrawableProcessorSettings::CalculatePipelineStateHash());
        CombineHash(hash, OcclusionBufferSettings::CalculatePipelineStateHash());
        CombineHash(hash, ShadowMapAllocatorSettings::CalculatePipelineStateHash());
        CombineHash(hash, InstancingBufferSettings::CalculatePipelineStateHash());
        CombineHash(hash, BatchRendererSettings::CalculatePipelineStateHash());
        CombineHash(hash, enableShadows_);
        CombineHash(hash, deferredLighting_);
        return hash;
    }

    bool operator==(const SceneProcessorSettings& rhs) const
    {
        return DrawableProcessorSettings::operator==(rhs)
            && OcclusionBufferSettings::operator==(rhs)
            && ShadowMapAllocatorSettings::operator==(rhs)
            && InstancingBufferSettings::operator==(rhs)
            && BatchRendererSettings::operator==(rhs)
            && enableShadows_ == rhs.enableShadows_
            && deferredLighting_ == rhs.deferredLighting_;
    }

    bool operator!=(const SceneProcessorSettings& rhs) const { return !(*this == rhs); }
    /// @}
};

}
