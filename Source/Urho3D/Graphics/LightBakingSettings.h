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

/// \file

#pragma once

#include "../Math/Vector3.h"
#include "../Resource/ImageCube.h"

#include <EASTL/string.h>

namespace Urho3D
{

/// Lightmap chart allocation settings.
struct LightmapChartingSettings
{
    /// Size of lightmap chart.
    unsigned lightmapSize_{ 512 };
    /// Padding between individual objects on the chart.
    unsigned padding_{ 1 };
    /// Texel density in texels per Scene unit.
    float texelDensity_{ 10 };
    /// Minimal scale of object lightmaps.
    /// Values below 1 may cause lightmap bleeding due to insufficient padding.
    /// Values above 0 may cause inconsistent lightmap density if object scale is too small.
    float minObjectScale_{ 1.0f };
    /// Default chart size for models w/o metadata. Don't rely on it.
    unsigned defaultChartSize_{ 16 };
};

/// Lightmap geometry buffer baking settings.
struct LightmapGeometryBakingSettings
{
    /// Baking render path.
    ea::string renderPathName_{ "RenderPaths/LightmapGBuffer.xml" };
    /// Baking materials.
    ea::string materialName_{ "Materials/LightmapBaker.xml" };
    /// Lightmap UV channel. 2nd channel by default.
    unsigned uvChannel_{ 1 };
    /// Position bias in geometry buffer in direction of face normal. Scaled with position itself.
    float scaledPositionBias_{ 0.00002f };
    /// Constant position bias.
    float constantPositionBias_{};
};

/// Settings for geometry buffer preprocessing.
struct GeometryBufferPreprocessSettings
{
    /// Number of tasks to spawn.
    unsigned numTasks_{ 1 };
    /// Determines how much position is pushed from behind backface to prevent shadow bleeding.
    float constPositionBackfaceBias_{ 0.0f };
    /// Determines how much position is pushed from behind backface to prevent shadow bleeding. Scaled with position itself.
    float scaledPositionBackfaceBias_{ 0.00002f };
};

/// Parameters of emission light tracing.
struct EmissionLightTracingSettings
{
    /// Number of tasks to spawn.
    unsigned numTasks_{ 1 };
};

/// Parameters of direct light tracing.
struct DirectLightTracingSettings
{
    /// Construct default.
    DirectLightTracingSettings() = default;
    /// Construct for given max samples.
    explicit DirectLightTracingSettings(unsigned maxSamples)
        : maxSamples_(maxSamples)
    {
    }

    /// Number of tasks to spawn.
    unsigned numTasks_{ 1 };
    /// Max number of samples per element.
    unsigned maxSamples_{ 10 };
};

/// Parameters of indirect light tracing.
struct IndirectLightTracingSettings
{
    /// Max number of bounces.
    static const unsigned MaxBounces = 8;

    /// Construct default.
    IndirectLightTracingSettings() = default;
    /// Construct for given max samples and bounces.
    IndirectLightTracingSettings(unsigned maxSamples, unsigned maxBounces)
        : maxSamples_(maxSamples)
        , maxBounces_(maxBounces)
    {
    }

    /// Number of tasks to spawn.
    unsigned numTasks_{ 1 };
    /// Max number of samples per element.
    unsigned maxSamples_{ 10 };
    /// Max number of bounces.
    unsigned maxBounces_{ 2 };
    /// Position bias in direction of face normal after hit. Scaled with position.
    float scaledPositionBounceBias_{ 0.00002f };
    /// Constant position bias in direction of face normal after hit.
    float constPositionBounceBias_{ 0.0f };
};

/// Parameters for indirect light filtering.
struct EdgeStoppingGaussFilterParameters
{
    /// Kernel radius.
    int kernelRadius_{ 2 };
    /// Upscale factor for offsets.
    int upscale_{ 1 };
    /// Color weight. The lesser value is, the more color details are preserved on flat surface.
    float luminanceSigma_{ 10.0f };
    /// Normal weight. The higher value is, the more color details are preserved on normal edges.
    float normalPower_{ 4.0f };
    /// Position weight. The lesser value is, the more color details are preserved on position edges.
    float positionSigma_{ 1.0f };
};

/// Lightmap stitching settings.
struct LightmapStitchingSettings
{
    /// Number of iterations.
    unsigned numIterations_{ 8 };
    /// Blend factor.
    float blendFactor_{ 0.5f };
    /// Model used for background during stitching.
    ea::string stitchBackgroundModelName_{ "Models/Plane.mdl" };
    /// Technique used for background during stitching.
    ea::string stitchBackgroundTechniqueName_{ "Techniques/DiffUnlit.xml" };
    /// Technique used for seams rendering during stitching.
    ea::string stitchSeamsTechniqueName_{ "Techniques/DiffUnlitAlpha.xml" };
};

/// Light calculation properties that can be used to adjust result.
struct LightCalculationProperties
{
    /// Emission light brightness multiplier.
    float emissionBrightness_{ 1.0f };
};

/// Incremental light baker settings.
struct IncrementalLightBakerSettings
{
    /// Size of the chunk.
    Vector3 chunkSize_ = Vector3::ONE * 128.0f;
    /// Additional space around chunk to collect indirect lighting.
    float indirectPadding_ = 32.0f;
    /// Shadow casting distance for directional light.
    float directionalLightShadowDistance_ = 128.0f;
    /// Output directory name.
    ea::string outputDirectory_;
    /// Global illumination data file.
    ea::string giDataFileName_{ "GI.bin" };
    /// Lightmap name format string.
    /// Placeholder 1: global lightmap index.
    ea::string lightmapNameFormat_{ "Textures/Lightmap-{}.png" };
    /// Light probe group name format string.
    /// Placeholders 1-3: x, y and z components of chunk index.
    /// Placeholder 4: light probe group index within chunk.
    ea::string lightProbeGroupNameFormat_{ "Binary/LightProbeGroup-{}-{}-{}-{}.bin" };
};

/// Aggregated light baking settings.
struct LightBakingSettings
{
    /// Charting settings.
    LightmapChartingSettings charting_;
    /// Geometry baking settings.
    LightmapGeometryBakingSettings geometryBufferBaking_;
    /// Geometry buffer preprocessing settings.
    GeometryBufferPreprocessSettings geometryBufferPreprocessing_;

    /// Settings for emission light tracing.
    EmissionLightTracingSettings emissionTracing_;
    /// Settings for direct light tracing for charts.
    DirectLightTracingSettings directChartTracing_{ 10 };
    /// Settings for direct light tracing for light probes.
    DirectLightTracingSettings directProbesTracing_{ 32 };
    /// Settings for indirect light tracing for charts.
    IndirectLightTracingSettings indirectChartTracing_{ 10, 2 };
    /// Settings for indirect light tracing for light probes.
    IndirectLightTracingSettings indirectProbesTracing_{ 64, 2 };

    /// Direct light filtering settings.
    EdgeStoppingGaussFilterParameters directFilter_{ 2 };
    /// Indirect light filtering settings.
    EdgeStoppingGaussFilterParameters indirectFilter_{ 5 };

    /// Stitching settings.
    LightmapStitchingSettings stitching_;

    /// Calculation properties.
    LightCalculationProperties properties_;

    /// Incremental light baker settings.
    IncrementalLightBakerSettings incremental_;
};

}
