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

#include "../Glow/BakedLight.h"
#include "../Glow/BakedSceneCollector.h"
#include "../Glow/LightmapGeometryBuffer.h"
#include "../Glow/RaytracerScene.h"
#include "../Graphics/LightProbeGroup.h"

namespace Urho3D
{

/// Light probe collection with extra data needed for baking.
struct LightProbeCollectionForBaking : public LightProbeCollection
{
    /// Size is the same as number of probes.
    /// @{
    ea::vector<unsigned> lightMasks_;
    ea::vector<unsigned> backgroundIds_;
    /// @}
};

/// Baking chunk. Contains everything to bake light for given chunk.
struct BakedSceneChunk
{
    /// Lightmaps owned by this chunk.
    ea::vector<unsigned> lightmaps_;
    /// Direct lightmaps required to bake this chunk.
    ea::vector<unsigned> requiredDirectLightmaps_;

    /// Raytracer scene.
    SharedPtr<RaytracerScene> raytracerScene_;
    /// Geometry buffers.
    ea::vector<LightmapChartGeometryBuffer> geometryBuffers_;
    /// Geometry buffer ID to raytracer geometry ID mapping.
    ea::vector<unsigned> geometryBufferToRaytracer_;
    /// Lights to bake.
    ea::vector<BakedLight> bakedLights_;
    /// Light probes collection.
    LightProbeCollectionForBaking lightProbesCollection_;
    /// Number of unique light probe groups. Used for saving results.
    unsigned numUniqueLightProbes_{};
};

/// Create baked scene chunk.
URHO3D_API BakedSceneChunk CreateBakedSceneChunk(Context* context,
    BakedSceneCollector& collector, const IntVector3& chunk, const LightBakingSettings& settings);

}
