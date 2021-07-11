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

#include "../Graphics/Model.h"
#include "../Graphics/ModelView.h"
#include "../Graphics/StaticModel.h"
#include "../Graphics/Terrain.h"
#include "../Graphics/TerrainPatch.h"
#include "../Glow/RaytracerScene.h"
#include "../Glow/Helpers.h"
#include "../IO/Log.h"

#include <embree3/rtcore.h>
#include <embree3/rtcore_ray.h>

#include <future>

using namespace embree3;

namespace Urho3D
{

namespace
{

/// Return raytracing geometry material.
RaytracingGeometryMaterial CreateRaytracingGeometryMaterial(const Material* material)
{
    RaytracingGeometryMaterial raytracingMaterial;

    raytracingMaterial.opaque_ = !material || IsMaterialOpaque(material);
    if (!raytracingMaterial.opaque_)
    {
        const Color diffuseColor = GetMaterialDiffuseColor(material);
        raytracingMaterial.diffuseColor_ = diffuseColor.ToVector3();
        raytracingMaterial.alpha_ = diffuseColor.a_;

        Texture* diffuseTexture = GetMaterialDiffuseTexture(material,
            raytracingMaterial.uOffset_, raytracingMaterial.vOffset_);
        if (diffuseTexture)
        {
            raytracingMaterial.storeUV_ = true;
            raytracingMaterial.diffuseImageName_ = diffuseTexture->GetName();
        }
    }

    return raytracingMaterial;
}

/// Parameters of lightmapped raytracing geometry.
struct LightmappedRaytracingGeometryParams
{
    /// Whether the geometry is for direct shadows only and is not lightmapped.
    bool directShadowsOnly_{};
    /// Whether the geometry is primary LOD.
    bool primaryLod_{};
    /// Lightmap index.
    unsigned lightmapIndex_{};
    /// Lightmap UV scale.
    Vector2 lightmapUVScale_;
    /// Lightmap UV offset.
    Vector2 lightmapUVOffset_;
    /// UV channel used for lightmap UV.
    unsigned lightmapUVChannel_{};

    /// Return transformed lightmap UV.
    Vector2 ConvertUV(const Vector2& uv) const
    {
        return uv * lightmapUVScale_ + lightmapUVOffset_;
    }

    /// Return whether the lightmap UV and smooth normals are needed.
    bool AreLightmapUVsAndNormalsNeeded() const { return !directShadowsOnly_ && primaryLod_; }

    /// Return geometry mask to use.
    unsigned GetMask() const
    {
        if (directShadowsOnly_)
            return RaytracerScene::DirectShadowOnlyGeometry;
        else if (primaryLod_)
            return RaytracerScene::PrimaryLODGeometry;
        else
            return RaytracerScene::SecondaryLODGeometry;
    }
};

/// Parameters for raytracing geometry creation from geometry view.
struct RaytracingFromGeometryViewParams
{
    /// Node name.
    ea::string nodeName_;
    /// Transform from geometry to world space.
    Matrix3x4 worldTransform_;
    /// Rotation from geometry to world space.
    Quaternion worldRotation_;
    /// Unpacked geometry data.
    const GeometryLODView* geometry_{};
    /// Material.
    RaytracingGeometryMaterial material_;
    /// Lightmapping parameters.
    LightmappedRaytracingGeometryParams lightmapping_;
};

/// Parameters for raytracing geometry creation from terrain.
struct RaytracingFromTerrainParams
{
    /// Terrain.
    const Terrain* terrain_{};
    /// Material.
    RaytracingGeometryMaterial material_;
    /// Lightmapping parameters.
    LightmappedRaytracingGeometryParams lightmapping_;
};

/// Pair of model and corresponding model view.
struct ModelModelViewPair
{
    Model* model_{};
    SharedPtr<ModelView> parsedModel_;
};

/// Parse model data.
ModelModelViewPair ParseModelForRaytracer(Model* model, bool needLightmapUVAndNormal, unsigned uvChannel)
{
    auto modelView = MakeShared<ModelView>(model->GetContext());
    modelView->ImportModel(model);

    const ModelVertexFormat vertexFormat = modelView->GetVertexFormat();
    const bool missingPosition = vertexFormat.position_ == ModelVertexFormat::Undefined;
    const bool missingNormal = vertexFormat.normal_ == ModelVertexFormat::Undefined;
    const bool missingUv1 = vertexFormat.uv_[uvChannel] == ModelVertexFormat::Undefined;

    if (missingPosition || (needLightmapUVAndNormal && (missingNormal || missingUv1)))
    {
        URHO3D_LOGERROR("Model \"{}\" doesn't have required vertex attributes", model->GetName());
        return {};
    }

    return { model, modelView };
}

/// Create Embree geometry from geometry view.
RTCGeometry CreateEmbreeGeometryForGeometryView(RTCDevice embreeDevice, const RaytracingFromGeometryViewParams& params)
{
    const unsigned numVertices = params.geometry_->vertices_.size();
    const unsigned numIndices = params.geometry_->indices_.size();
    const ea::vector<ModelVertex>& sourceVertices = params.geometry_->vertices_;

    RTCGeometry embreeGeometry = rtcNewGeometry(embreeDevice, RTC_GEOMETRY_TYPE_TRIANGLE);
    rtcSetGeometryVertexAttributeCount(embreeGeometry, RaytracerScene::MaxAttributes);

    float* vertices = reinterpret_cast<float*>(rtcSetNewGeometryBuffer(embreeGeometry, RTC_BUFFER_TYPE_VERTEX,
        0, RTC_FORMAT_FLOAT3, sizeof(Vector3), numVertices));

    float* lightmapUVs = nullptr;
    float* smoothNormals = nullptr;

    if (params.lightmapping_.AreLightmapUVsAndNormalsNeeded())
    {
        lightmapUVs = reinterpret_cast<float*>(rtcSetNewGeometryBuffer(embreeGeometry, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE,
            RaytracerScene::LightmapUVAttribute, RTC_FORMAT_FLOAT2, sizeof(Vector2), numVertices));

        smoothNormals = reinterpret_cast<float*>(rtcSetNewGeometryBuffer(embreeGeometry, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE,
            RaytracerScene::NormalAttribute, RTC_FORMAT_FLOAT3, sizeof(Vector3), numVertices));
    }

    float* uvs = nullptr;
    if (params.material_.storeUV_)
    {
        uvs = reinterpret_cast<float*>(rtcSetNewGeometryBuffer(embreeGeometry, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE,
            RaytracerScene::UVAttribute, RTC_FORMAT_FLOAT2, sizeof(Vector2), numVertices));
    }

    bool errorReported = false;
    for (unsigned i = 0; i < numVertices; ++i)
    {
        const Vector3 localPosition = static_cast<Vector3>(sourceVertices[i].position_);
        const Vector3 worldPosition = params.worldTransform_ * localPosition;

        vertices[i * 3 + 0] = worldPosition.x_;
        vertices[i * 3 + 1] = worldPosition.y_;
        vertices[i * 3 + 2] = worldPosition.z_;

        if (lightmapUVs)
        {
            const Vector2 lightmapUV = static_cast<Vector2>(sourceVertices[i].uv_[params.lightmapping_.lightmapUVChannel_]);
            const Vector2 lightmapUVScaled = params.lightmapping_.ConvertUV(lightmapUV);
            const Vector2 lightmapUVClamped = VectorMax(Vector2::ZERO, VectorMin(lightmapUVScaled, Vector2::ONE));

            if (!errorReported && lightmapUVScaled != lightmapUVClamped)
            {
                errorReported = true;
                URHO3D_LOGWARNING("Lightmap UVs for node {} are clamped, lighting may be incorrect", params.nodeName_);
            }

            lightmapUVs[i * 2 + 0] = lightmapUVClamped.x_;
            lightmapUVs[i * 2 + 1] = lightmapUVClamped.y_;
        }

        if (smoothNormals)
        {
            const Vector3 localNormal = static_cast<Vector3>(sourceVertices[i].normal_);
            const Vector3 worldNormal = params.worldRotation_ * localNormal;

            smoothNormals[i * 3 + 0] = worldNormal.x_;
            smoothNormals[i * 3 + 1] = worldNormal.y_;
            smoothNormals[i * 3 + 2] = worldNormal.z_;
        }

        if (uvs)
        {
            const Vector2 uv = static_cast<Vector2>(sourceVertices[i].uv_[0]);
            const Vector2 uvScaled = params.material_.ConvertUV(uv);
            const Vector2 uvClamped = VectorMax(Vector2::ZERO, VectorMin(uvScaled, Vector2::ONE));

            if (!errorReported && uvScaled != uvClamped)
            {
                errorReported = true;
                URHO3D_LOGWARNING("UVs for node {} are clamped, lighting may be incorrect", params.nodeName_);
            }

            uvs[i * 2 + 0] = uvClamped.x_;
            uvs[i * 2 + 1] = uvClamped.y_;
        }
    }

    unsigned* indices = reinterpret_cast<unsigned*>(rtcSetNewGeometryBuffer(embreeGeometry, RTC_BUFFER_TYPE_INDEX,
        0, RTC_FORMAT_UINT3, sizeof(unsigned) * 3, numIndices / 3));

    for (unsigned i = 0; i < numIndices; ++i)
        indices[i] = params.geometry_->indices_[i];

    rtcSetGeometryMask(embreeGeometry, params.lightmapping_.GetMask());
    rtcCommitGeometry(embreeGeometry);
    return embreeGeometry;
}

/// Create Embree geometry from geometry view.
RTCGeometry CreateEmbreeGeometryForTerrain(RTCDevice embreeDevice, const RaytracingFromTerrainParams& params)
{
    const Terrain* terrain = params.terrain_;
    const IntVector2 terrainSize = terrain->GetNumVertices();
    const IntVector2 numPatches = terrain->GetNumPatches();
    const int patchSize = terrain->GetPatchSize();

    const unsigned numVertices = static_cast<unsigned>(terrainSize.x_ * terrainSize.y_);
    const unsigned numQuads = static_cast<unsigned>(numPatches.x_ * numPatches.y_ * patchSize * patchSize);

    RTCGeometry embreeGeometry = rtcNewGeometry(embreeDevice, RTC_GEOMETRY_TYPE_TRIANGLE);
    rtcSetGeometryVertexAttributeCount(embreeGeometry, 3);

    float* vertices = reinterpret_cast<float*>(rtcSetNewGeometryBuffer(embreeGeometry, RTC_BUFFER_TYPE_VERTEX,
        0, RTC_FORMAT_FLOAT3, sizeof(Vector3), numVertices));

    float* lightmapUVs = nullptr;
    float* smoothNormals = nullptr;

    if (params.lightmapping_.AreLightmapUVsAndNormalsNeeded())
    {
        lightmapUVs = reinterpret_cast<float*>(rtcSetNewGeometryBuffer(embreeGeometry, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE,
            RaytracerScene::LightmapUVAttribute, RTC_FORMAT_FLOAT2, sizeof(Vector2), numVertices));

        smoothNormals = reinterpret_cast<float*>(rtcSetNewGeometryBuffer(embreeGeometry, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE,
            RaytracerScene::NormalAttribute, RTC_FORMAT_FLOAT3, sizeof(Vector3), numVertices));
    }

    float* uvs = nullptr;
    if (params.material_.storeUV_)
    {
        uvs = reinterpret_cast<float*>(rtcSetNewGeometryBuffer(embreeGeometry, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE,
            RaytracerScene::UVAttribute, RTC_FORMAT_FLOAT2, sizeof(Vector2), numVertices));
    }

    for (unsigned i = 0; i < numVertices; ++i)
    {
        const int x = static_cast<int>(i) % terrainSize.x_;
        const int y = terrainSize.y_ - static_cast<int>(i) / terrainSize.x_ - 1;
        const Vector3 worldPosition = terrain->HeightMapToWorld({ x, y });
        const Vector2 uv = terrain->HeightMapToUV({ x, y });

        vertices[i * 3 + 0] = worldPosition.x_;
        vertices[i * 3 + 1] = worldPosition.y_;
        vertices[i * 3 + 2] = worldPosition.z_;

        if (lightmapUVs)
        {
            const Vector2 lightmapUVScaled = params.lightmapping_.ConvertUV(uv);

            lightmapUVs[i * 2 + 0] = lightmapUVScaled.x_;
            lightmapUVs[i * 2 + 1] = lightmapUVScaled.y_;
        }

        if (smoothNormals)
        {
            const Vector3 worldNormal = terrain->GetNormal(worldPosition);

            smoothNormals[i * 3 + 0] = worldNormal.x_;
            smoothNormals[i * 3 + 1] = worldNormal.y_;
            smoothNormals[i * 3 + 2] = worldNormal.z_;
        }

        if (params.material_.storeUV_)
        {
            const Vector2 uvScaled = params.material_.ConvertUV(uv);
            uvs[i * 2 + 0] = uvScaled.x_;
            uvs[i * 2 + 1] = uvScaled.y_;
        }
    }

    unsigned* indices = reinterpret_cast<unsigned*>(rtcSetNewGeometryBuffer(embreeGeometry, RTC_BUFFER_TYPE_INDEX,
        0, RTC_FORMAT_UINT3, sizeof(unsigned) * 3, numQuads * 2 * 3));

    const unsigned maxZ = numPatches.y_ * patchSize;
    const unsigned maxX = numPatches.x_ * patchSize;
    const unsigned row = terrainSize.x_;
    unsigned i = 0;
    for (unsigned z = 0; z < numPatches.y_ * patchSize; ++z)
    {
        for (unsigned x = 0; x < numPatches.x_ * patchSize; ++x)
        {
            indices[i++] = (z + 1) * row + x;
            indices[i++] = z * row + x + 1;
            indices[i++] = z * row + x;
            indices[i++] = (z + 1) * row + x;
            indices[i++] = (z + 1) * row + x + 1;
            indices[i++] = z * row + x + 1;
        }
    }
    assert(i == numQuads * 2 * 3);

    rtcSetGeometryMask(embreeGeometry, params.lightmapping_.GetMask());
    rtcCommitGeometry(embreeGeometry);
    return embreeGeometry;
}

/// Create raytracer geometries for static model.
ea::vector<RaytracerGeometry> CreateRaytracerGeometriesForStaticModel(RTCDevice embreeDevice,
    ModelView* modelView, StaticModel* staticModel, unsigned objectIndex, unsigned lightmapUVChannel)
{
    auto renderer = staticModel->GetContext()->GetSubsystem<Renderer>();

    Node* node = staticModel->GetNode();
    const Vector4& lightmapUVScaleOffset = staticModel->GetLightmapScaleOffset();

    RaytracingFromGeometryViewParams params;
    params.nodeName_ = node->GetName();
    params.worldTransform_ = node->GetWorldTransform();
    params.worldRotation_ = node->GetWorldRotation();
    params.lightmapping_.directShadowsOnly_ = !staticModel->GetBakeLightmapEffective();
    params.lightmapping_.lightmapIndex_ = staticModel->GetLightmapIndex();
    params.lightmapping_.lightmapUVScale_ = { lightmapUVScaleOffset.x_, lightmapUVScaleOffset.y_ };
    params.lightmapping_.lightmapUVOffset_ = { lightmapUVScaleOffset.z_, lightmapUVScaleOffset.w_ };
    params.lightmapping_.lightmapUVChannel_ = lightmapUVChannel;

    if (params.lightmapping_.directShadowsOnly_)
        params.lightmapping_.lightmapIndex_ = M_MAX_UNSIGNED;

    ea::vector<RaytracerGeometry> result;

    const ea::vector<GeometryView> geometries = modelView->GetGeometries();
    for (unsigned geometryIndex = 0; geometryIndex < geometries.size(); ++geometryIndex)
    {
        const Material* material = staticModel->GetMaterial(geometryIndex);
        if (!material)
            material = renderer->GetDefaultMaterial();

        const GeometryView& geometryView = geometries[geometryIndex];
        for (unsigned lodIndex = 0; lodIndex < geometryView.lods_.size(); ++lodIndex)
        {
            const GeometryLODView& geometryLODView = geometryView.lods_[lodIndex];

            RaytracerGeometry raytracerGeometry;
            raytracerGeometry.objectIndex_ = objectIndex;
            raytracerGeometry.geometryIndex_ = geometryIndex;
            raytracerGeometry.lodIndex_ = lodIndex;
            raytracerGeometry.numLods_ = geometryView.lods_.size();
            raytracerGeometry.lightmapIndex_ = params.lightmapping_.lightmapIndex_;
            raytracerGeometry.raytracerGeometryId_ = M_MAX_UNSIGNED;
            raytracerGeometry.material_ = CreateRaytracingGeometryMaterial(material);

            params.geometry_ = &geometryLODView;
            params.material_ = raytracerGeometry.material_;
            params.lightmapping_.primaryLod_ = lodIndex == 0;

            raytracerGeometry.embreeGeometry_ = CreateEmbreeGeometryForGeometryView(embreeDevice, params);
            result.push_back(raytracerGeometry);
        }
    }
    return result;
}

/// Create raytracer geometry for terrain.
ea::vector<RaytracerGeometry> CreateRaytracerGeometriesForTerrain(RTCDevice embreeDevice,
    Terrain* terrain, unsigned objectIndex, unsigned lightmapUVChannel)
{
    auto renderer = terrain->GetContext()->GetSubsystem<Renderer>();

    const Material* material = terrain->GetMaterial();
    if (!material)
        material = renderer->GetDefaultMaterial();

    const Vector4& lightmapUVScaleOffset = terrain->GetLightmapScaleOffset();

    RaytracingFromTerrainParams params;
    params.lightmapping_.primaryLod_ = true;
    params.lightmapping_.directShadowsOnly_ = !terrain->GetBakeLightmapEffective();
    params.lightmapping_.lightmapIndex_ = terrain->GetLightmapIndex();
    params.lightmapping_.lightmapUVScale_ = { lightmapUVScaleOffset.x_, lightmapUVScaleOffset.y_ };
    params.lightmapping_.lightmapUVOffset_ = { lightmapUVScaleOffset.z_, lightmapUVScaleOffset.w_ };
    params.lightmapping_.lightmapUVChannel_ = lightmapUVChannel;

    if (params.lightmapping_.directShadowsOnly_)
        params.lightmapping_.lightmapIndex_ = M_MAX_UNSIGNED;

    RaytracerGeometry raytracerGeometry;
    raytracerGeometry.objectIndex_ = objectIndex;
    raytracerGeometry.geometryIndex_ = 0;
    raytracerGeometry.lodIndex_ = 0;
    raytracerGeometry.numLods_ = 1;
    raytracerGeometry.lightmapIndex_ = params.lightmapping_.lightmapIndex_;
    raytracerGeometry.raytracerGeometryId_ = M_MAX_UNSIGNED;
    raytracerGeometry.material_ = CreateRaytracingGeometryMaterial(material);

    params.terrain_ = terrain;
    params.material_ = raytracerGeometry.material_;

    raytracerGeometry.embreeGeometry_ = CreateEmbreeGeometryForTerrain(embreeDevice, params);
    return { raytracerGeometry };
}

}

RaytracerScene::~RaytracerScene()
{
    if (scene_)
        rtcReleaseScene(scene_);
    if (device_)
        rtcReleaseDevice(device_);
}

SharedPtr<RaytracerScene> CreateRaytracingScene(Context* context, const ea::vector<Component*>& geometries,
    unsigned lightmapUVChannel, const BakedSceneBackgroundArrayPtr& backgrounds)
{
    // Queue models for parsing.
    // Value determines whether the model needs lightmap UV and smooth normal.
    ea::hash_map<Model*, bool> modelsToParse;
    for (Component* geometry : geometries)
    {
        if (auto staticModel = dynamic_cast<StaticModel*>(geometry))
        {
            const bool directShadowOnly = !staticModel->GetBakeLightmapEffective();
            bool& needLightmapUVAndNormal = modelsToParse[staticModel->GetModel()];
            needLightmapUVAndNormal = needLightmapUVAndNormal && !directShadowOnly;
        }
    }

    // Start model parsing
    ea::vector<std::future<ModelModelViewPair>> modelParseTasks;
    for (const auto& item : modelsToParse)
        modelParseTasks.push_back(std::async(ParseModelForRaytracer, item.first, item.second, lightmapUVChannel));

    // Finish model parsing
    ea::unordered_map<Model*, SharedPtr<ModelView>> parsedModelCache;
    for (auto& task : modelParseTasks)
    {
        const ModelModelViewPair& parsedModel = task.get();
        parsedModelCache.emplace(parsedModel.model_, parsedModel.parsedModel_);
    }

    // Prepare Embree scene
    const RTCDevice device = rtcNewDevice("");
    const RTCScene scene = rtcNewScene(device);
    rtcSetSceneFlags(scene, RTC_SCENE_FLAG_CONTEXT_FILTER_FUNCTION);

    ea::vector<std::future<ea::vector<RaytracerGeometry>>> createRaytracerGeometriesTasks;
    for (unsigned objectIndex = 0; objectIndex < geometries.size(); ++objectIndex)
    {
        Component* geometry = geometries[objectIndex];
        if (auto staticModel = dynamic_cast<StaticModel*>(geometry))
        {
            if (ModelView* parsedModel = parsedModelCache[staticModel->GetModel()])
            {
                createRaytracerGeometriesTasks.push_back(std::async(CreateRaytracerGeometriesForStaticModel,
                    device, parsedModel, staticModel, objectIndex, lightmapUVChannel));
            }
        }
        else if (auto terrain = dynamic_cast<Terrain*>(geometry))
        {
            createRaytracerGeometriesTasks.push_back(std::async(CreateRaytracerGeometriesForTerrain,
                device, terrain, objectIndex, lightmapUVChannel));
        }
    }

    // Collect and attach Embree geometries
    ea::hash_map<ea::string, SharedPtr<Image>> diffuseImages;
    ea::vector<RaytracerGeometry> geometryIndex;
    for (auto& task : createRaytracerGeometriesTasks)
    {
        const ea::vector<RaytracerGeometry> raytracerGeometryArray = task.get();
        for (const RaytracerGeometry& raytracerGeometry : raytracerGeometryArray)
        {
            const unsigned geomID = rtcAttachGeometry(scene, raytracerGeometry.embreeGeometry_);
            rtcReleaseGeometry(raytracerGeometry.embreeGeometry_);

            geometryIndex.resize(geomID + 1);
            geometryIndex[geomID] = raytracerGeometry;
            geometryIndex[geomID].raytracerGeometryId_ = geomID;
            diffuseImages[raytracerGeometry.material_.diffuseImageName_] = nullptr;
        }
    }

    // Finalize scene
    rtcCommitScene(scene);

    // Load images
    auto cache = context->GetSubsystem<ResourceCache>();
    for (auto& nameAndImage : diffuseImages)
    {
        if (!nameAndImage.first.empty())
        {
            auto image = cache->GetResource<Image>(nameAndImage.first);
            nameAndImage.second = image->GetDecompressedImage();
        }
    }

    for (RaytracerGeometry& raytracerGeometry : geometryIndex)
    {
        raytracerGeometry.material_.diffuseImage_ = diffuseImages[raytracerGeometry.material_.diffuseImageName_];
        if (raytracerGeometry.material_.diffuseImage_)
        {
            raytracerGeometry.material_.diffuseImageWidth_ = raytracerGeometry.material_.diffuseImage_->GetWidth();
            raytracerGeometry.material_.diffuseImageHeight_ = raytracerGeometry.material_.diffuseImage_->GetHeight();
        }
    }

    // Calculate max distance between objects
    BoundingBox boundingBox;
    for (Component* geometry : geometries)
    {
        if (auto staticModel = dynamic_cast<StaticModel*>(geometry))
            boundingBox.Merge(staticModel->GetWorldBoundingBox());
        else if (auto terrain = dynamic_cast<Terrain*>(geometry))
            boundingBox.Merge(terrain->CalculateWorldBoundingBox());
    }

    const Vector3 sceneSize = boundingBox.Size();
    const float maxDistance = ea::max({ sceneSize.x_, sceneSize.y_, sceneSize.z_ });

    return MakeShared<RaytracerScene>(context, device, scene, ea::move(geometryIndex), backgrounds, maxDistance);
}

}
