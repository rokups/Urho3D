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

#include "../Glow/BakedSceneChunk.h"

#include "../Glow/Helpers.h"
#include "../Glow/LightTracer.h"
#include "../Graphics/Drawable.h"
#include "../Graphics/Terrain.h"
#include "../Graphics/TerrainPatch.h"
#include "../Graphics/Zone.h"
#include "../IO/Log.h"

#include <EASTL/sort.h>

namespace Urho3D
{

namespace
{

/// Calculate frustum containing all shadow casters for given volume and light direction.
Frustum CalculateDirectionalLightFrustum(const BoundingBox& boundingBox,
    const Vector3& lightDirection, float distance, float angle)
{
    const Quaternion rotation{ Vector3::DOWN, lightDirection };
    const float widthPadding = distance * Sin(angle);
    BoundingBox lightSpaceBoundingBox = boundingBox.Transformed(rotation.Inverse().RotationMatrix());
    lightSpaceBoundingBox.min_.x_ -= widthPadding;
    lightSpaceBoundingBox.min_.z_ -= widthPadding;
    lightSpaceBoundingBox.max_.x_ += widthPadding;
    lightSpaceBoundingBox.max_.z_ += widthPadding;
    lightSpaceBoundingBox.max_.y_ += distance;

    Frustum frustum;
    frustum.Define(lightSpaceBoundingBox, static_cast<Matrix3x4>(rotation.RotationMatrix()));
    return frustum;
}

/// Collect lights required to bake chunk.
ea::vector<Light*> CollectLightsInChunk(BakedSceneCollector& collector, const IntVector3& chunk)
{
    const BoundingBox lightReceiversBoundingBox = collector.GetChunkBoundingBox(chunk);
    return collector.GetLightsInBoundingBox(chunk, lightReceiversBoundingBox);
}

/// Collect geometries required to bake chunk.
ea::vector<Component*> CollectGeometriesInChunk(BakedSceneCollector& collector, const IntVector3& chunk,
    const ea::vector<Component*>& uniqueGeometries, const ea::vector<Light*>& lightsInChunk,
    float directionalLightShadowDistance, float indirectPadding)
{
    const BoundingBox lightReceiversBoundingBox = collector.GetChunkBoundingBox(chunk);

    // Collect shadow casters for direct lighting
    ea::hash_set<Component*> relevantGeometries;
    for (Light* light : lightsInChunk)
    {
        if (light->GetLightType() == LIGHT_DIRECTIONAL)
        {
            const Vector3 direction = light->GetNode()->GetWorldDirection();
            const Frustum frustum = CalculateDirectionalLightFrustum(lightReceiversBoundingBox, direction,
                directionalLightShadowDistance, 0.0f);
            const ea::vector<Component*> shadowCasters = collector.GetGeometriesInFrustum(chunk, frustum);
            relevantGeometries.insert(shadowCasters.begin(), shadowCasters.end());
        }
        else
        {
            BoundingBox extendedBoundingBox = lightReceiversBoundingBox;
            extendedBoundingBox.Merge(light->GetNode()->GetWorldPosition());
            BoundingBox shadowCastersBoundingBox = light->GetWorldBoundingBox();
            shadowCastersBoundingBox.Clip(extendedBoundingBox);
            const ea::vector<Component*> shadowCasters = collector.GetGeometriesInBoundingBox(
                chunk, shadowCastersBoundingBox);
            relevantGeometries.insert(shadowCasters.begin(), shadowCasters.end());
        }
    }

    // Collect light receivers for indirect lighting propagation
    BoundingBox indirectBoundingBox = lightReceiversBoundingBox;
    indirectBoundingBox.min_ -= Vector3::ONE * indirectPadding;
    indirectBoundingBox.max_ += Vector3::ONE * indirectPadding;

    const ea::vector<Component*> indirectStaticModels = collector.GetGeometriesInBoundingBox(
        chunk, indirectBoundingBox);
    relevantGeometries.insert(indirectStaticModels.begin(), indirectStaticModels.end());

    // Collect light receivers, unique are first
    for (Component* geometry : uniqueGeometries)
        relevantGeometries.erase(geometry);

    ea::vector<Component*> geometriesInChunk = uniqueGeometries;
    geometriesInChunk.insert(geometriesInChunk.end(), relevantGeometries.begin(), relevantGeometries.end());

    return geometriesInChunk;
}

/// Collect light probe groups in chunk.
ea::vector<LightProbeGroup*> CollectLightProbeGroupsInChunk(
    BakedSceneCollector& collector, const IntVector3& chunk, const ea::vector<LightProbeGroup*>& uniqueLightProbeGroups)
{
    const BoundingBox lightReceiversBoundingBox = collector.GetChunkBoundingBox(chunk);

    const ea::vector<LightProbeGroup*> lightProbesInVolume = collector.GetLightProbeGroupsInBoundingBox(
        chunk, lightReceiversBoundingBox);
    ea::hash_set<LightProbeGroup*> relevantLightProbes(lightProbesInVolume.begin(), lightProbesInVolume.end());

    for (LightProbeGroup* group : uniqueLightProbeGroups)
        relevantLightProbes.erase(group);

    ea::vector<LightProbeGroup*> lightProbeGroupsInChunk = uniqueLightProbeGroups;
    lightProbeGroupsInChunk.insert(lightProbeGroupsInChunk.end(), relevantLightProbes.begin(), relevantLightProbes.end());

    return lightProbeGroupsInChunk;
}

/// Create mapping from geometry buffer to raytracing scene.
ea::vector<unsigned> CreateGeometryMapping(
    const GeometryIDToObjectMappingVector& idToObject, const ea::vector<RaytracerGeometry>& raytracerGeometries)
{
    ea::vector<RaytracerGeometry> raytracerGeometriesSorted = raytracerGeometries;
    bool matching = idToObject.size() <= raytracerGeometriesSorted.size() + 1;
    if (matching)
    {
        ea::sort(raytracerGeometriesSorted.begin(), raytracerGeometriesSorted.end(), CompareRaytracerGeometryByObject);
        for (unsigned i = 1; i < idToObject.size(); ++i)
        {
            const RaytracerGeometry& raytracerGeometry = raytracerGeometriesSorted[i - 1];
            const GeometryIDToObjectMapping& mapping = idToObject[i];
            if (raytracerGeometry.objectIndex_ != mapping.objectIndex_
                || raytracerGeometry.geometryIndex_ != mapping.geometryIndex_
                || raytracerGeometry.lodIndex_ != mapping.lodIndex_)
            {
                matching = false;
                break;
            }
        }
    }

    if (!matching)
    {
        URHO3D_LOGERROR("Cannot match raytracer geometries with lightmap G-Buffer");
    }

    ea::vector<unsigned> geometryBufferToRaytracerGeometry;
    geometryBufferToRaytracerGeometry.resize(idToObject.size(), M_MAX_UNSIGNED);
    if (matching)
    {
        for (unsigned i = 1; i < idToObject.size(); ++i)
            geometryBufferToRaytracerGeometry[i] = raytracerGeometriesSorted[i - 1].raytracerGeometryId_;
    }

    return geometryBufferToRaytracerGeometry;
}

/// Create baked lights.
ea::vector<BakedLight> CreateBakedLights(const ea::vector<Light*>& lightsInChunk)
{
    ea::vector<BakedLight> bakedLights;
    for (Light* light : lightsInChunk)
        bakedLights.push_back(BakedLight{ light });
    return bakedLights;
}

/// Collect lightmaps in chunk.
ea::vector<unsigned> CollectLightmapsInChunk(const LightmapChartGeometryBufferVector& geometryBuffers)
{
    ea::vector<unsigned> lightmapsInChunk;
    for (const LightmapChartGeometryBuffer& geometryBuffer : geometryBuffers)
        lightmapsInChunk.push_back(geometryBuffer.index_);
    return lightmapsInChunk;
}

/// Collect direct lightmaps required for chunk.
ea::vector<unsigned> CollectLightmapsRequiredForChunk(const ea::vector<RaytracerGeometry>& raytracerGeometries)
{
    ea::hash_set<unsigned> requiredDirectLightmaps;
    for (const RaytracerGeometry& raytracerGeometry : raytracerGeometries)
    {
        if (raytracerGeometry.lightmapIndex_ != M_MAX_UNSIGNED)
            requiredDirectLightmaps.insert(raytracerGeometry.lightmapIndex_);
    }
    return ea::vector<unsigned>(requiredDirectLightmaps.begin(), requiredDirectLightmaps.end());
}

}

BakedSceneChunk CreateBakedSceneChunk(Context* context,
    BakedSceneCollector& collector, const IntVector3& chunk, const LightBakingSettings& settings)
{
    // Collect objects
    const ea::vector<Component*> uniqueGeometries = collector.GetUniqueGeometries(chunk);
    const ea::vector<LightProbeGroup*> uniqueLightProbeGroups = collector.GetUniqueLightProbeGroups(chunk);

    const ea::vector<Light*> lightsInChunk = CollectLightsInChunk(collector, chunk);
    ea::vector<LightProbeGroup*> lightProbeGroupsInChunk = CollectLightProbeGroupsInChunk(
        collector, chunk, uniqueLightProbeGroups);

    const ea::vector<Component*> geometriesInChunk = CollectGeometriesInChunk(
        collector, chunk, uniqueGeometries, lightsInChunk,
        settings.incremental_.directionalLightShadowDistance_, settings.incremental_.indirectPadding_);

    // Bake geometry buffers
    const LightmapGeometryBakingScenesArray geometryBakingScenes = GenerateLightmapGeometryBakingScenes(
        context, uniqueGeometries, settings.charting_.lightmapSize_, settings.geometryBufferBaking_);
    LightmapChartGeometryBufferVector geometryBuffers = BakeLightmapGeometryBuffers(
        geometryBakingScenes.bakingScenes_);

    // Collect light probes, unique are first
    LightProbeCollectionForBaking lightProbesCollection;
    LightProbeGroup::CollectLightProbes(lightProbeGroupsInChunk, lightProbesCollection, nullptr);

    // Fill baking info for light probes
    for (unsigned i = 0; i < lightProbeGroupsInChunk.size(); ++i)
    {
        LightProbeGroup* lightProbeGroup = lightProbeGroupsInChunk[i];
        Zone* zone = collector.GetLightProbeGroupZone(chunk, lightProbeGroup);

        const unsigned numProbes = lightProbesCollection.counts_[i];
        const unsigned lightMask = zone->GetLightMask() & lightProbeGroup->GetLightMask();
        const unsigned backgroundId = collector.GetZoneBackground(chunk, zone);

        lightProbesCollection.lightMasks_.insert(lightProbesCollection.lightMasks_.end(), numProbes, lightMask);
        lightProbesCollection.backgroundIds_.insert(lightProbesCollection.backgroundIds_.end(), numProbes, backgroundId);
    }

    const unsigned uvChannel = settings.geometryBufferBaking_.uvChannel_;
    const SharedPtr<RaytracerScene> raytracerScene = CreateRaytracingScene(
        context, geometriesInChunk, uvChannel, collector.GetBackgrounds());

    // Match raytracer geometries and geometry buffer
    ea::vector<unsigned> geometryBufferToRaytracerGeometry = CreateGeometryMapping(
        geometryBakingScenes.idToObject_, raytracerScene->GetGeometries());

    // Preprocess geometry buffers
    for (LightmapChartGeometryBuffer& geometryBuffer : geometryBuffers)
    {
        PreprocessGeometryBuffer(geometryBuffer, *raytracerScene, geometryBufferToRaytracerGeometry,
            settings.geometryBufferPreprocessing_);

        ParallelFor(geometryBuffer.positions_.size(), settings.geometryBufferPreprocessing_.numTasks_,
            [&](unsigned fromIndex, unsigned toIndex)
        {
            for (unsigned i = fromIndex; i < toIndex; ++i)
            {
                const unsigned geometryId = geometryBuffer.geometryIds_[i];
                if (!geometryId)
                    continue;

                const unsigned objectIndex = geometryBakingScenes.idToObject_[geometryId].objectIndex_;
                Component* geometry = uniqueGeometries[objectIndex];

                if (auto terrain = dynamic_cast<Terrain*>(geometry))
                {
                    // Use effective light mask of central patch for terrain
                    const IntVector2 numPatches = terrain->GetNumPatches();
                    const IntVector2 patchIndex = VectorMin(numPatches / 2, numPatches - IntVector2::ONE);
                    Drawable* drawable = terrain->GetPatch(patchIndex.x_, patchIndex.y_);
                    Zone* zone = drawable->GetMutableCachedZone().zone_;
                    geometryBuffer.lightMasks_[i] = drawable->GetLightMaskInZone();
                    geometryBuffer.backgroundIds_[i] = collector.GetZoneBackground(chunk, zone);
                }
                else if (auto drawable = dynamic_cast<Drawable*>(geometry))
                {
                    Zone* zone = drawable->GetMutableCachedZone().zone_;
                    geometryBuffer.lightMasks_[i] = drawable->GetLightMaskInZone();
                    geometryBuffer.backgroundIds_[i] = collector.GetZoneBackground(chunk, zone);
                }
            }
        });
    }

    // Create baked chunk
    BakedSceneChunk bakedChunk;
    bakedChunk.lightmaps_ = CollectLightmapsInChunk(geometryBuffers);
    bakedChunk.requiredDirectLightmaps_ = CollectLightmapsRequiredForChunk(raytracerScene->GetGeometries());
    bakedChunk.raytracerScene_ = raytracerScene;
    bakedChunk.geometryBuffers_ = ea::move(geometryBuffers);
    bakedChunk.geometryBufferToRaytracer_ = ea::move(geometryBufferToRaytracerGeometry);
    bakedChunk.bakedLights_ = CreateBakedLights(lightsInChunk);
    bakedChunk.lightProbesCollection_ = ea::move(lightProbesCollection);
    bakedChunk.numUniqueLightProbes_ = uniqueLightProbeGroups.size();

    return bakedChunk;
}

}
