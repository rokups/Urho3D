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

#include "../Graphics/Light.h"
#include "../Math/SphericalHarmonics.h"

#include <EASTL/fixed_vector.h>
#include <EASTL/sort.h>
#include <EASTL/vector_multimap.h>

namespace Urho3D
{

/// Context used for light accumulation.
struct DrawableLightDataAccumulationContext
{
    /// Max number of pixel lights
    unsigned maxPixelLights_{ 1 };
    /// Light importance.
    LightImportance lightImportance_{};
    /// Light index.
    unsigned lightIndex_{};
    /// Array of lights to be indexed.
    const ea::vector<Light*>* lights_;
};

/// Accumulated light data for drawable.
/// MaxPixelLights: Max number of per-pixel lights supported. Important lights may override this limit.
/// MaxVertexLights: Max number of per-vertex lights supported.
template <unsigned MaxPixelLights, unsigned MaxVertexLights>
struct DrawableLightAccumulator
{
    /// Max number of lights that don't require allocations.
    static const unsigned NumElements = ea::max(MaxPixelLights + 1, 4u) + MaxVertexLights;
    /// Container for lights.
    using Container = ea::vector_multimap<float, unsigned, ea::less<float>, ea::allocator,
        ea::fixed_vector<ea::pair<float, unsigned>, NumElements>>;
    /// Container for vertex lights.
    using VertexLightContainer = ea::array<unsigned, MaxVertexLights>;

    /// Reset accumulator.
    void Reset()
    {
        lights_.clear();
        numImportantLights_ = 0;
        numAutoLights_ = 0;
    }

    /// Accumulate light.
    void AccumulateLight(const DrawableLightDataAccumulationContext& ctx, float penalty)
    {
        switch (ctx.lightImportance_)
        {
        case LI_IMPORTANT:
            // Important lights are never optimized out
            penalty = -1.0f;
            ++numImportantLights_;
            break;
        case LI_AUTO:
            // Penalty for automatic lights is mapped to [0, 2]
            if (penalty > 1.0f)
                penalty = 2.0f - 1.0f / penalty;
            ++numAutoLights_;
            break;
        case LI_NOT_IMPORTANT:
            // Penalty for not important lights is mapped to [3, 5]
            if (penalty <= 1.0f)
                penalty = 3.0f + penalty;
            else
                penalty = 5.0f - 1.0f / penalty;
            break;
        }

        // Add new light
        lights_.emplace(penalty, ctx.lightIndex_);

        // First N important and automatic lights are per-pixel
        firstVertexLight_ = ea::max(numImportantLights_, ea::min(numImportantLights_ + numAutoLights_, ctx.maxPixelLights_));

        // If too many lights, drop the least important one
        const unsigned maxLights = MaxVertexLights + firstVertexLight_;
        if (lights_.size() > maxLights)
        {
            // TODO(renderer): Update SH
            lights_.pop_back();
        }
    }

    /// Return per-vertex lights.
    VertexLightContainer GetVertexLights() const
    {
        VertexLightContainer vertexLights;
        for (unsigned i = 0; i < 4; ++i)
        {
            const unsigned index = i + firstVertexLight_;
            vertexLights[i] = index < lights_.size() ? lights_.at(index).second : M_MAX_UNSIGNED;
        }
        return vertexLights;
    }

    /// Return per-pixel lights.
    ea::span<const ea::pair<float, unsigned>> GetPixelLights() const
    {
        return { lights_.data(), ea::min(static_cast<unsigned>(lights_.size()), firstVertexLight_) };
    }

    /// Container of per-pixel and per-pixel lights.
    Container lights_;
    /// Accumulated SH lights.
    SphericalHarmonicsDot9 sh_;
    /// Number of important lights.
    unsigned numImportantLights_{};
    /// Number of automatic lights.
    unsigned numAutoLights_{};
    /// First vertex light.
    unsigned firstVertexLight_{};
};

}
