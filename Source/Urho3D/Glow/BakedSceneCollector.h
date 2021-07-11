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

#include "../Glow/BakedSceneBackground.h"
#include "../Math/Frustum.h"
#include "../Math/Vector3.h"
#include "../Resource/ImageCube.h"

#include <EASTL/vector.h>

namespace Urho3D
{

class Component;
class Drawable;
class Light;
class LightProbeGroup;
class Node;
class Scene;
class Octree;
class Zone;

/// Interface of scene collector for light baking.
/// Objects may be loaded and unloaded even if scene is locked if it doesn't affect the outcome.
class URHO3D_API BakedSceneCollector
{
public:
    /// Destruct.
    virtual ~BakedSceneCollector();

    /// Called before everything else. Scene objects must stay unchanged after this call.
    virtual void LockScene(Scene* scene, const Vector3& chunkSize) = 0;
    /// Return all scene chunks.
    virtual ea::vector<IntVector3> GetChunks() = 0;
    /// Return all scene backgrounds. [0] is expected to be pitch-black background.
    virtual BakedSceneBackgroundArrayPtr GetBackgrounds() = 0;

    /// Return unique geometries within chunk.
    virtual ea::vector<Component*> GetUniqueGeometries(const IntVector3& chunkIndex) = 0;
    /// Called when geometries were changed externally.
    virtual void CommitGeometries(const IntVector3& chunkIndex) = 0;
    /// Return unique light probe groups within chunk. Order of groups must stay the same for each call.
    virtual ea::vector<LightProbeGroup*> GetUniqueLightProbeGroups(const IntVector3& chunkIndex) = 0;
    /// Return zone that corresponds to light probe group within chunk.
    virtual Zone* GetLightProbeGroupZone(const IntVector3& chunkIndex, LightProbeGroup* lightProbeGroup) = 0;
    /// Return background index for zone within chunk.
    virtual unsigned GetZoneBackground(const IntVector3& chunkIndex, Zone* zone) = 0;

    /// Return bounding box of unique nodes of the chunk.
    virtual BoundingBox GetChunkBoundingBox(const IntVector3& chunkIndex) = 0;
    /// Return lights intersecting given volume.
    virtual ea::vector<Light*> GetLightsInBoundingBox(const IntVector3& chunkIndex, const BoundingBox& boundingBox) = 0;
    /// Return geometries intersecting given volume.
    virtual ea::vector<Component*> GetGeometriesInBoundingBox(const IntVector3& chunkIndex, const BoundingBox& boundingBox) = 0;
    /// Return light probe groups intersecting given volume.
    virtual ea::vector<LightProbeGroup*> GetLightProbeGroupsInBoundingBox(const IntVector3& chunkIndex, const BoundingBox& boundingBox) = 0;
    /// Return geometries intersecting given frustum. The frustum is guaranteed to contain specified chunk.
    virtual ea::vector<Component*> GetGeometriesInFrustum(const IntVector3& chunkIndex, const Frustum& frustum) = 0;

    /// Called after everything else. Scene objects must stay unchanged until this call.
    virtual void UnlockScene() = 0;
};

/// Standard scene collector for light baking.
class URHO3D_API DefaultBakedSceneCollector : public BakedSceneCollector
{
public:
    DefaultBakedSceneCollector() {}

    /// BakedSceneCollector implementation
    /// @{
    void LockScene(Scene* scene, const Vector3& chunkSize) override;
    ea::vector<IntVector3> GetChunks() override;
    BakedSceneBackgroundArrayPtr GetBackgrounds() override;

    ea::vector<Component*> GetUniqueGeometries(const IntVector3& chunkIndex) override;
    void CommitGeometries(const IntVector3& chunkIndex) override;
    ea::vector<LightProbeGroup*> GetUniqueLightProbeGroups(const IntVector3& chunkIndex) override;
    Zone* GetLightProbeGroupZone(const IntVector3& chunkIndex, LightProbeGroup* lightProbeGroup) override;
    unsigned GetZoneBackground(const IntVector3& chunkIndex, Zone* zone) override;

    BoundingBox GetChunkBoundingBox(const IntVector3& chunkIndex) override;
    ea::vector<Light*> GetLightsInBoundingBox(const IntVector3& chunkIndex, const BoundingBox& boundingBox) override;
    ea::vector<Component*> GetGeometriesInBoundingBox(const IntVector3& chunkIndex, const BoundingBox& boundingBox) override;
    ea::vector<LightProbeGroup*> GetLightProbeGroupsInBoundingBox(const IntVector3& chunkIndex, const BoundingBox& boundingBox) override;
    ea::vector<Component*> GetGeometriesInFrustum(const IntVector3& chunkIndex, const Frustum& frustum) override;

    void UnlockScene() override;
    /// @}

private:
    /// Filter drawables and return relevant components.
    ea::vector<Component*> CollectGeometriesFromDrawables(const ea::vector<Drawable*> drawables);
    /// Chunk data.
    struct ChunkData
    {
        /// Unique geometries.
        ea::vector<Component*> geometries_;
        /// Unique light probe groups.
        ea::vector<LightProbeGroup*> lightProbeGroups_;
        /// Bounding box.
        BoundingBox boundingBox_;
    };

    /// Scene.
    Scene* scene_{};
    /// Chunk size.
    Vector3 chunkSize_;
    /// Bounding box of the scene.
    BoundingBox boundingBox_;
    /// Dimensions of chunk grid.
    IntVector3 chunkGridDimension_;
    /// Scene Octree.
    Octree* octree_{};

    /// All relevant objects within scene
    /// @{
    ea::unordered_map<IntVector3, ChunkData> chunks_;
    ea::vector<LightProbeGroup*> lightProbeGroups_;
    ea::vector<Zone*> zones_;
    /// @}

    /// Baking backgrounds
    /// @{
    BakedSceneBackgroundArrayPtr backgrounds_;
    ea::unordered_map<Zone*, unsigned> zoneToBackgroundMap_;
    /// @}
};

}
