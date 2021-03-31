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

#include "../Glow/LightmapGeometryBuffer.h"

#include "../Core/Context.h"
#include "../IO/Log.h"
#include "../Math/BoundingBox.h"
#include "../Glow/Helpers.h"
#include "../Glow/LightmapUVGenerator.h"
#include "../Glow/StaticModelForLightmap.h"
#include "../Graphics/Camera.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Material.h"
#include "../Graphics/Model.h"
#include "../Graphics/ModelView.h"
#include "../Graphics/Octree.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/StaticModel.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/Terrain.h"
#include "../Graphics/TerrainPatch.h"
#include "../RenderPipeline/LightmapRenderPipeline.h"
#include "../Resource/ResourceCache.h"
#include "../Resource/XMLFile.h"
#include "../Scene/Node.h"

#include <EASTL/sort.h>

#include <future>

namespace Urho3D
{

namespace
{

/// Number of multi-tap samples.
const unsigned numMultiTapSamples = 25;

/// Multi-tap offsets.
const Vector2 multiTapOffsets[numMultiTapSamples] =
{
    {  1.0f,  1.0f },
    {  1.0f, -1.0f },
    { -1.0f,  1.0f },
    { -1.0f, -1.0f },

    {  1.0f,  0.5f },
    {  1.0f, -0.5f },
    { -1.0f,  0.5f },
    { -1.0f, -0.5f },
    {  0.5f,  1.0f },
    {  0.5f, -1.0f },
    { -0.5f,  1.0f },
    { -0.5f, -1.0f },

    {  1.0f,  0.0f },
    { -1.0f,  0.0f },
    {  0.0f,  1.0f },
    {  0.0f, -1.0f },

    {  0.5f,  0.5f },
    {  0.5f, -0.5f },
    { -0.5f,  0.5f },
    { -0.5f, -0.5f },

    {  0.5f,  0.0f },
    { -0.5f,  0.0f },
    {  0.0f,  0.5f },
    {  0.0f, -0.5f },

    {  0.0f,  0.0f },
};

/// Pair of two ordered indices.
using OrderedIndexPair = ea::pair<unsigned, unsigned>;

/// Return edge by two indices.
OrderedIndexPair MakeOrderedIndexPair(unsigned firstIndex, unsigned secondIndex)
{
    if (firstIndex < secondIndex)
        return { firstIndex, secondIndex };
    else
        return { secondIndex, firstIndex };
}

/// Collect seams of the model.
LightmapSeamVector CollectModelSeams(Model* model, unsigned uvChannel)
{
    ModelView modelView(model->GetContext());
    if (!modelView.ImportModel(model))
    {
        URHO3D_LOGERROR("Cannot import model \"{}\"", model->GetName());
        return {};
    }

    const bool sharedLightmapUV = modelView.GetMetadata(LightmapUVGenerationSettings::LightmapSharedUV).GetBool();

    // Calculate bounding box and step for spatial hashing
    const float positionEpsilon = M_LARGE_EPSILON;
    const float positionEpsilonSquared = positionEpsilon * positionEpsilon;
    const float normalEpsilon = M_LARGE_EPSILON;
    const float normalEpsilonSquared = normalEpsilon * normalEpsilon;
    const float uvEpsilon = M_LARGE_EPSILON;
    const float uvEpsilonSquared = uvEpsilon * uvEpsilon;

    const BoundingBox boundingBox = modelView.CalculateBoundingBox();
    const Vector3 hashStep = VectorMax(boundingBox.Size() / M_LARGE_VALUE, Vector3::ONE * positionEpsilon);
    const auto computeHash = [&](const Vector3& position)
    {
        return VectorFloorToInt((position - boundingBox.min_) / hashStep);
    };

    const ModelVertexFormat vertexFormat = modelView.GetVertexFormat();
    if (vertexFormat.position_ == ModelVertexFormat::Undefined
        || vertexFormat.normal_ == ModelVertexFormat::Undefined
        || vertexFormat.uv_[uvChannel] == ModelVertexFormat::Undefined)
    {
        URHO3D_LOGERROR("Model \"{}\" doesn't have required vertex attributes", model->GetName());
        return {};
    }

    ea::vector<LightmapSeam> seams;
    for (const GeometryView& geometry : modelView.GetGeometries())
    {
        if (geometry.lods_.empty())
            continue;

        for (const GeometryLODView& geometryLod : geometry.lods_)
        {
            const ea::vector<ModelVertex>& vertices = geometryLod.vertices_;

            // Read all edges
            ea::vector<OrderedIndexPair> geometryEdges;
            for (unsigned faceIndex = 0; faceIndex < geometryLod.indices_.size() / 3; ++faceIndex)
            {
                const unsigned indexA = geometryLod.indices_[faceIndex * 3 + 0];
                const unsigned indexB = geometryLod.indices_[faceIndex * 3 + 1];
                const unsigned indexC = geometryLod.indices_[faceIndex * 3 + 2];

                geometryEdges.push_back(MakeOrderedIndexPair(indexA, indexB));
                geometryEdges.push_back(MakeOrderedIndexPair(indexB, indexC));
                geometryEdges.push_back(MakeOrderedIndexPair(indexC, indexA));
            }

            // Remove duplicates
            ea::sort(geometryEdges.begin(), geometryEdges.end());
            geometryEdges.erase(ea::unique(geometryEdges.begin(), geometryEdges.end()), geometryEdges.end());

            // Make spatial hash for edges.
            ea::unordered_map<IntVector3, ea::vector<OrderedIndexPair>> geometryEdgesHash;
            for (const OrderedIndexPair& edge : geometryEdges)
            {
                // Hash both sides of the edge
                for (const unsigned index : { edge.first, edge.second })
                {
                    const ModelVertex& vertex = vertices[index];
                    const Vector3& position = static_cast<Vector3>(vertex.position_);
                    const IntVector3 hashPosition = computeHash(position);
                    geometryEdgesHash[hashPosition].push_back(edge);
                }
            }

            // Find seams
            ea::vector<OrderedIndexPair> candidatesBuffer;
            for (const OrderedIndexPair& edge : geometryEdges)
            {
                // Find candidates from spatial hash
                candidatesBuffer.clear();
                for (const unsigned index : { edge.first, edge.second })
                {
                    const ModelVertex& vertex = vertices[index];
                    const Vector3& position = static_cast<Vector3>(vertex.position_);
                    const IntVector3 hashPosition = computeHash(position);

                    IntVector3 hashOffset;
                    for (hashOffset.x_ = -1; hashOffset.x_ <= 1; ++hashOffset.x_)
                    {
                        for (hashOffset.y_ = -1; hashOffset.y_ <= 1; ++hashOffset.y_)
                        {
                            for (hashOffset.z_ = -1; hashOffset.z_ <= 1; ++hashOffset.z_)
                            {
                                const auto iter = geometryEdgesHash.find(hashPosition + hashOffset);
                                if (iter != geometryEdgesHash.end())
                                    candidatesBuffer.append(iter->second);
                            }
                        }
                    }
                }

                // Remove duplicates
                ea::sort(candidatesBuffer.begin(), candidatesBuffer.end());
                candidatesBuffer.erase(ea::unique(candidatesBuffer.begin(), candidatesBuffer.end()), candidatesBuffer.end());

                // Check for seams
                const Vector3 edgePos0 = static_cast<Vector3>(vertices[edge.first].position_);
                const Vector3 edgePos1 = static_cast<Vector3>(vertices[edge.second].position_);
                const Vector3 edgeNormal0 = static_cast<Vector3>(vertices[edge.first].normal_);
                const Vector3 edgeNormal1 = static_cast<Vector3>(vertices[edge.second].normal_);
                const Vector2 edgeUv0 = static_cast<Vector2>(vertices[edge.first].uv_[uvChannel]);
                const Vector2 edgeUv1 = static_cast<Vector2>(vertices[edge.second].uv_[uvChannel]);

                for (OrderedIndexPair candidate : candidatesBuffer)
                {
                    // Skip self
                    if (candidate == edge)
                        continue;

                    // Swap candidate vertices if needed
                    {
                        const Vector3 candidatePos0 = static_cast<Vector3>(vertices[candidate.first].position_);
                        if ((candidatePos0 - edgePos1).LengthSquared() < positionEpsilonSquared)
                            ea::swap(candidate.first, candidate.second);
                    }

                    const Vector3 candidatePos0 = static_cast<Vector3>(vertices[candidate.first].position_);
                    const Vector3 candidatePos1 = static_cast<Vector3>(vertices[candidate.second].position_);
                    const Vector3 candidateNormal0 = static_cast<Vector3>(vertices[candidate.first].normal_);
                    const Vector3 candidateNormal1 = static_cast<Vector3>(vertices[candidate.second].normal_);
                    const Vector2 candidateUv0 = static_cast<Vector2>(vertices[candidate.first].uv_[uvChannel]);
                    const Vector2 candidateUv1 = static_cast<Vector2>(vertices[candidate.second].uv_[uvChannel]);

                    // Skip if edge geometry is different
                    const bool samePos0 = (edgePos0 - candidatePos0).LengthSquared() < positionEpsilonSquared;
                    const bool samePos1 = (edgePos1 - candidatePos1).LengthSquared() < positionEpsilonSquared;
                    const bool sameNormal0 = (edgeNormal0 - candidateNormal0).LengthSquared() < normalEpsilonSquared;
                    const bool sameNormal1 = (edgeNormal1 - candidateNormal1).LengthSquared() < normalEpsilonSquared;
                    if (!samePos0 || !samePos1 || !sameNormal0 || !sameNormal1)
                        continue;

                    // Skip if not a seam
                    const bool sameUv0 = (edgeUv0 - candidateUv0).LengthSquared() < uvEpsilonSquared;
                    const bool sameUv1 = (edgeUv1 - candidateUv1).LengthSquared() < uvEpsilonSquared;
                    if (sameUv0 && sameUv1)
                        continue;

                    // Skip if belong to the same line: AB x AC = AB x AD = 0
                    const Vector3 edgeUvDelta{ edgeUv1 - edgeUv0, 0.0f };
                    const Vector3 delta00{ candidateUv0 - edgeUv0, 0.0f };
                    const Vector3 delta01{ candidateUv1 - edgeUv0, 0.0f };
                    const bool collinear00 = edgeUvDelta.CrossProduct(delta00).LengthSquared() < uvEpsilonSquared;
                    const bool collinear01 = edgeUvDelta.CrossProduct(delta01).LengthSquared() < uvEpsilonSquared;
                    if (collinear00 && collinear01)
                        continue;

                    // It's a seam!
                    LightmapSeam seam;
                    seam.positions_[0] = edgeUv0;
                    seam.positions_[1] = edgeUv1;
                    seam.otherPositions_[0] = candidateUv0;
                    seam.otherPositions_[1] = candidateUv1;
                    seams.push_back(seam);
                }
            }

            // Skip the rest of lods if UVs are shared
            if (sharedLightmapUV)
                break;
        }
    }
    return seams;
}

/// Read RGBA32 float texture to vector.
void ReadTextureRGBA32Float(Texture* texture, ea::vector<Vector4>& dest)
{
    auto texture2D = dynamic_cast<Texture2D*>(texture);
    const unsigned numElements = texture->GetDataSize(texture->GetWidth(), texture->GetHeight()) / sizeof(Vector4);
    dest.resize(numElements);
    texture2D->GetData(0, dest.data());
}

/// Extract Vector3 from Vector4.
Vector3 ExtractVector3FromVector4(const Vector4& data) { return { data.x_, data.y_, data.z_ }; }

/// Extract w-component as unsigned integer from Vector4.
unsigned ExtractUintFromVector4(const Vector4& data) { return static_cast<unsigned>(data.w_); }

/// Extract w-component as float from Vector4.
float ExtractFloatFromVector4(const Vector4& data) { return data.w_; }

}

LightmapGeometryBakingScenesArray GenerateLightmapGeometryBakingScenes(
    Context* context, const ea::vector<Component*>& geometries,
    unsigned lightmapSize, const LightmapGeometryBakingSettings& settings)
{
    const Vector2 texelSize{ 1.0f / lightmapSize, 1.0f / lightmapSize };
    const Vector2 scaledAndConstBias{ settings.scaledPositionBias_, settings.constantPositionBias_ };

    Material* bakingMaterial = context->GetSubsystem<ResourceCache>()->GetResource<Material>(settings.materialName_);
    if (!bakingMaterial)
    {
        URHO3D_LOGERROR("Cannot load material \"{}\"", settings.materialName_);
        return {};
    }

    // Collect used models
    ea::hash_set<Model*> usedModels;
    for (Component* geometry : geometries)
    {
        if (auto staticModel = dynamic_cast<StaticModel*>(geometry))
            usedModels.insert(staticModel->GetModel());
    }

    // Schedule model seams collecting
    ea::vector<std::future<ea::pair<Model*, LightmapSeamVector>>> collectSeamsTasks;
    for (Model* model : usedModels)
    {
        const unsigned uvChannel = settings.uvChannel_;
        collectSeamsTasks.push_back(std::async([model, uvChannel]()
        {
            LightmapSeamVector modelSeams = CollectModelSeams(model, uvChannel);
            return ea::make_pair(model, ea::move(modelSeams));
        }));
    }

    // Cache model seams
    ea::hash_map<Model*, LightmapSeamVector> modelSeamsCache;
    for (auto& task : collectSeamsTasks)
    {
        auto result = task.get();
        modelSeamsCache.emplace(result.first, ea::move(result.second));
    }

    // Zero ID is reserved for invalid texels
    GeometryIDToObjectMappingVector mapping;
    mapping.push_back();

    ea::unordered_map<unsigned, LightmapGeometryBakingScene> bakingScenes;
    for (unsigned objectIndex = 0; objectIndex < geometries.size(); ++objectIndex)
    {
        // Extract input parameters
        Component* geometry = geometries[objectIndex];
        Node* node = geometry->GetNode();
        const unsigned lightmapIndex = GetLightmapIndex(geometry);
        const Vector4 scaleOffset = GetLightmapScaleOffset(geometry);
        const Vector2 scale{ scaleOffset.x_, scaleOffset.y_ };
        const Vector2 offset{ scaleOffset.z_, scaleOffset.w_ };

        // Initialize baking scene if first hit
        LightmapGeometryBakingScene& bakingScene = bakingScenes[lightmapIndex];
        if (!bakingScene.context_)
        {
            bakingScene.context_ = context;
            bakingScene.index_ = lightmapIndex;
            bakingScene.lightmapSize_ = lightmapSize;

            bakingScene.scene_ = MakeShared<Scene>(context);
            bakingScene.scene_->CreateComponent<Octree>();

            Node* cameraNode = bakingScene.scene_->CreateChild();
            cameraNode->SetPosition(Vector3::BACK * M_LARGE_VALUE);
            bakingScene.camera_ = cameraNode->CreateComponent<Camera>();
            bakingScene.camera_->SetFarClip(M_LARGE_VALUE * 2);
            bakingScene.camera_->SetOrthographic(true);
            bakingScene.camera_->SetOrthoSize(M_LARGE_VALUE * 2);
        }

        if (auto staticModel = dynamic_cast<StaticModel*>(geometry))
        {
            // Add node
            Node* bakingNode = bakingScene.scene_->CreateChild();
            bakingNode->SetPosition(node->GetWorldPosition());
            bakingNode->SetRotation(node->GetWorldRotation());
            bakingNode->SetScale(node->GetWorldScale());

            // Add seams
            const LightmapSeamVector& modelSeams = modelSeamsCache[staticModel->GetModel()];
            for (const LightmapSeam& seam : modelSeams)
                bakingScene.seams_.push_back(seam.Transformed(scale, offset));

            auto staticModelForLightmap = bakingNode->CreateComponent<StaticModelForLightmap>();
            const GeometryIDToObjectMappingVector objectMapping = staticModelForLightmap->Initialize(objectIndex,
                staticModel, bakingMaterial, mapping.size(), multiTapOffsets, texelSize, scaleOffset, scaledAndConstBias);

            mapping.append(objectMapping);
        }
        else if (auto terrain = dynamic_cast<Terrain*>(geometry))
        {
            for (unsigned tap = 0; tap < numMultiTapSamples; ++tap)
            {
                // Add node
                Node* bakingNode = bakingScene.scene_->CreateChild();
                bakingNode->SetPosition(node->GetWorldPosition());
                bakingNode->SetRotation(node->GetWorldRotation());
                bakingNode->SetScale(node->GetWorldScale());

                // Add terrain
                const Vector2 tapOffset = multiTapOffsets[tap] * texelSize;
                auto terrainForLightmap = bakingNode->CreateComponent<Terrain>();
                terrainForLightmap->SetMaxLodLevels(1);
                terrainForLightmap->SetSpacing(terrain->GetSpacing());
                terrainForLightmap->SetPatchSize(terrain->GetPatchSize());
                terrainForLightmap->SetSmoothing(terrain->GetSmoothing());

                // This is required to generate lightmap UV for terrain
                // but render terrain without lightmaps so it has valid emission texture.
                terrainForLightmap->SetBakeLightmap(true);
                terrainForLightmap->SetScaleInLightmap(0.0f);
                terrainForLightmap->SetHeightMap(terrain->GetHeightMap());

                SharedPtr<Material> material = CreateBakingMaterial(bakingMaterial, terrain->GetMaterial(),
                    scaleOffset, tap, numMultiTapSamples, tapOffset, mapping.size(), scaledAndConstBias);

                terrainForLightmap->SetMaterial(material);
            }

            GeometryIDToObjectMapping objectMapping;
            objectMapping.objectIndex_ = objectIndex;
            objectMapping.geometryIndex_ = 0;
            objectMapping.lodIndex_ = 0;
            mapping.push_back(objectMapping);
        }
    }

    ea::vector<LightmapGeometryBakingScene> result;
    for (auto& elem : bakingScenes)
        result.push_back(ea::move(elem.second));
    return { result, mapping };
}

LightmapChartGeometryBuffer BakeLightmapGeometryBuffer(const LightmapGeometryBakingScene& bakingScene)
{
    Context* context = bakingScene.context_;
    Graphics* graphics = context->GetSubsystem<Graphics>();
    Renderer* renderer = context->GetSubsystem<Renderer>();

    static thread_local ea::vector<Vector4> buffer;

    if (!graphics->BeginFrame())
    {
        URHO3D_LOGERROR("Failed to begin lightmap geometry buffer rendering \"{}\"");
        return {};
    }

    LightmapChartGeometryBuffer geometryBuffer{ bakingScene.index_, bakingScene.lightmapSize_ };

    // Setup viewport
    Viewport viewport(context);
    viewport.SetCamera(bakingScene.camera_);
    viewport.SetRect(IntRect::ZERO);
    viewport.SetScene(bakingScene.scene_);

    // Render scene
    LightmapRenderPipelineView view(context);
    view.RenderGeometryBuffer(&viewport, static_cast<int>(geometryBuffer.lightmapSize_));

    // Store results
    ReadTextureRGBA32Float(view.GetPositionBuffer(), buffer);
    ea::transform(buffer.begin(), buffer.end(), geometryBuffer.positions_.begin(), ExtractVector3FromVector4);
    ea::transform(buffer.begin(), buffer.end(), geometryBuffer.geometryIds_.begin(), ExtractUintFromVector4);

    ReadTextureRGBA32Float(view.GetSmoothPositionBuffer(), buffer);
    ea::transform(buffer.begin(), buffer.end(), geometryBuffer.smoothPositions_.begin(), ExtractVector3FromVector4);
    ea::transform(buffer.begin(), buffer.end(), geometryBuffer.texelRadiuses_.begin(), ExtractFloatFromVector4);

    ReadTextureRGBA32Float(view.GetFaceNormalBuffer(), buffer);
    ea::transform(buffer.begin(), buffer.end(), geometryBuffer.faceNormals_.begin(), ExtractVector3FromVector4);

    ReadTextureRGBA32Float(view.GetSmoothNormalBuffer(), buffer);
    ea::transform(buffer.begin(), buffer.end(), geometryBuffer.smoothNormals_.begin(), ExtractVector3FromVector4);

    ReadTextureRGBA32Float(view.GetAlbedoBuffer(), buffer);
    ea::transform(buffer.begin(), buffer.end(), geometryBuffer.albedo_.begin(), ExtractVector3FromVector4);

    ReadTextureRGBA32Float(view.GetEmissionBuffer(), buffer);
    ea::transform(buffer.begin(), buffer.end(), geometryBuffer.emission_.begin(), ExtractVector3FromVector4);

    graphics->EndFrame();

    geometryBuffer.seams_ = bakingScene.seams_;
    return geometryBuffer;
}

LightmapChartGeometryBufferVector BakeLightmapGeometryBuffers(const ea::vector<LightmapGeometryBakingScene>& bakingScenes)
{
    LightmapChartGeometryBufferVector geometryBuffers;
    for (const LightmapGeometryBakingScene& bakingScene : bakingScenes)
        geometryBuffers.push_back(BakeLightmapGeometryBuffer(bakingScene));
    return geometryBuffers;
}

}
