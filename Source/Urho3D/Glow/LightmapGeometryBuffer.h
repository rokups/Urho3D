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

#include "../Graphics/LightBakingSettings.h"
#include "../Graphics/RenderPath.h"
#include "../Scene/Scene.h"

namespace Urho3D
{

class Camera;
class Context;
class Component;

/// Used for mapping between geometry ID in geometry buffer and actual geometry.
struct GeometryIDToObjectMapping
{
    /// Index of the object in the array.
    unsigned objectIndex_{ M_MAX_UNSIGNED };
    /// Index of object geometry within the object.
    unsigned geometryIndex_{ M_MAX_UNSIGNED };
    /// Index of geometry LOD.
    unsigned lodIndex_{ M_MAX_UNSIGNED };
};

/// Vector of geometry mapping.
using GeometryIDToObjectMappingVector = ea::vector<GeometryIDToObjectMapping>;

/// Lightmap seam description.
struct LightmapSeam
{
    /// Edge on lightmap UV.
    Vector2 positions_[2];
    /// Other side of the edge on lightmap UV.
    Vector2 otherPositions_[2];
    /// Transform by scale and offset.
    LightmapSeam Transformed(const Vector2& scale, const Vector2& offset) const
    {
        LightmapSeam result;
        result.positions_[0] = positions_[0] * scale + offset;
        result.positions_[1] = positions_[1] * scale + offset;
        result.otherPositions_[0] = otherPositions_[0] * scale + offset;
        result.otherPositions_[1] = otherPositions_[1] * scale + offset;
        return result;
    }
};

/// Vector of lightmap seams.
using LightmapSeamVector = ea::vector<LightmapSeam>;

/// Baking scene for single lightmap chart.
struct LightmapGeometryBakingScene
{
    /// Context.
    Context* context_{};
    /// Lightmap chart index.
    unsigned index_{};
    /// Size of lightmap chart.
    unsigned lightmapSize_{};
    /// Baking scene.
    SharedPtr<Scene> scene_;
    /// Baking camera.
    Camera* camera_{};
    /// Lightmap seams.
    LightmapSeamVector seams_;
};

/// Baking scenes for the set of lightmap charts.
struct LightmapGeometryBakingScenesArray
{
    /// Baking scenes.
    ea::vector<LightmapGeometryBakingScene> bakingScenes_;
    /// Geometry buffer ID to object mapping.
    GeometryIDToObjectMappingVector idToObject_;
};

/// Generate baking scenes for lightmap charts.
URHO3D_API LightmapGeometryBakingScenesArray GenerateLightmapGeometryBakingScenes(
    Context* context, const ea::vector<Component*>& geometries,
    unsigned lightmapSize, const LightmapGeometryBakingSettings& settings);

/// Lightmap geometry buffer of lightmap chart.
struct LightmapChartGeometryBuffer
{
    /// Lightmap chart index.
    unsigned index_{};
    /// Size of lightmap chart.
    unsigned lightmapSize_{};

    /// Geometry buffer
    /// @{
    ea::vector<Vector3> positions_;
    ea::vector<Vector3> smoothPositions_; // TODO: Implement
    ea::vector<Vector3> smoothNormals_;
    ea::vector<Vector3> faceNormals_;
    ea::vector<unsigned> geometryIds_;
    ea::vector<unsigned> lightMasks_;
    ea::vector<unsigned> backgroundIds_;
    ea::vector<float> texelRadiuses_;
    ea::vector<Vector3> albedo_;
    ea::vector<Vector3> emission_;
    /// @}

    /// Lightmap seams.
    LightmapSeamVector seams_;

    /// Construct default.
    LightmapChartGeometryBuffer() = default;
    /// Construct valid.
    LightmapChartGeometryBuffer(unsigned index, unsigned size)
        : index_(index)
        , lightmapSize_(size)
        , positions_(lightmapSize_ * lightmapSize_)
        , smoothPositions_(lightmapSize_ * lightmapSize_)
        , smoothNormals_(lightmapSize_ * lightmapSize_)
        , faceNormals_(lightmapSize_ * lightmapSize_)
        , geometryIds_(lightmapSize_ * lightmapSize_)
        , lightMasks_(lightmapSize_ * lightmapSize_)
        , backgroundIds_(lightmapSize_ * lightmapSize_)
        , texelRadiuses_(lightmapSize_ * lightmapSize_)
        , albedo_(lightmapSize_ * lightmapSize_)
        , emission_(lightmapSize_ * lightmapSize_)
    {
    }
    /// Convert index to location.
    IntVector2 IndexToLocation(unsigned index) const
    {
        return { static_cast<int>(index % lightmapSize_), static_cast<int>(index / lightmapSize_) };
    }
    /// Returns whether the location is valid.
    bool IsValidLocation(const IntVector2& location) const
    {
        return 0 <= location.x_ && location.x_ < static_cast<int>(lightmapSize_)
            && 0 <= location.y_ && location.y_ < static_cast<int>(lightmapSize_);
    }
    /// Convert location to index.
    unsigned LocationToIndex(const IntVector2& location) const
    {
        return location.x_ + lightmapSize_ * location.y_;
    }
};

/// Vector of lightmap geometry buffers.
using LightmapChartGeometryBufferVector = ea::vector<LightmapChartGeometryBuffer>;

/// Bake lightmap geometry buffer for lightmap chart.
URHO3D_API LightmapChartGeometryBuffer BakeLightmapGeometryBuffer(const LightmapGeometryBakingScene& bakingScene);

/// Bake lightmap geometry buffer for lightmap charts.
URHO3D_API LightmapChartGeometryBufferVector BakeLightmapGeometryBuffers(
    const ea::vector<LightmapGeometryBakingScene>& bakingScenes);

}
