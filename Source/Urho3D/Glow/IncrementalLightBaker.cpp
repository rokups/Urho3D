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

#include "../Glow/IncrementalLightBaker.h"

#include "../Core/Context.h"
#include "../Glow/BakedSceneChunk.h"
#include "../Glow/LightmapCharter.h"
#include "../Glow/LightmapGeometryBuffer.h"
#include "../Glow/LightmapFilter.h"
#include "../Glow/LightmapStitcher.h"
#include "../Glow/LightTracer.h"
#include "../Glow/RaytracerScene.h"
#include "../Graphics/GlobalIllumination.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/LightProbeGroup.h"
#include "../Graphics/Model.h"
#include "../IO/FileSystem.h"
#include "../IO/Log.h"
#include "../Math/TetrahedralMesh.h"
#include "../Resource/Image.h"
#include "../Resource/ResourceCache.h"

#include <EASTL/algorithm.h>
#include <EASTL/numeric.h>
#include <EASTL/sort.h>

namespace Urho3D
{

namespace
{

/// Get resource name from file name.
ea::string GetResourceName(ResourceCache* cache, const ea::string& fileName)
{
    for (unsigned i = 0; i < cache->GetNumResourceDirs(); ++i)
    {
        const ea::string& resourceDir = cache->GetResourceDir(i);
        if (fileName.starts_with(resourceDir))
            return fileName.substr(resourceDir.length());
    }
    return {};
}

/// Per-component min for 3D integer vector.
IntVector3 MinIntVector3(const IntVector3& lhs, const IntVector3& rhs) { return VectorMin(lhs, rhs); }

/// Swizzle components of 3D integer vector.
unsigned long long Swizzle(const IntVector3& vec, const IntVector3& base = IntVector3::ZERO)
{
    static const unsigned numComponents = 3;
    static const unsigned resultBits = 8 * sizeof(unsigned long long);
    static const unsigned maxBitsPerComponent = resultBits / numComponents;

    const unsigned xyz[numComponents] = {
        static_cast<unsigned>(vec.x_ - base.x_),
        static_cast<unsigned>(vec.y_ - base.y_),
        static_cast<unsigned>(vec.z_ - base.z_),
    };

    unsigned result = 0;
    for (unsigned j = 0; j < numComponents; ++j)
    {
        for (unsigned i = 0; i < maxBitsPerComponent; ++i)
        {
            const unsigned long long bit = !!(xyz[j] & (1 << i));
            result |= bit << i * numComponents + j;
        }
    }

    return result;
}

}

/// Incremental light baker implementation.
struct IncrementalLightBaker::Impl
{
    /// Construct.
    Impl(const LightBakingSettings& settings, Scene* scene, BakedSceneCollector* collector, BakedLightCache* cache)
        : context_(scene->GetContext())
        , settings_(settings)
        , scene_(scene)
        , collector_(collector)
        , cache_(cache)
    {
    }

    /// Initialize.
    bool Initialize()
    {
        // DX9 is not supported
        if (Graphics::GetPixelUVOffset() != Vector2::ZERO)
        {
            URHO3D_LOGERROR("Cannot bake light on DX9");
            return false;
        }

        // Find or fix output directory
        if (settings_.incremental_.outputDirectory_.empty())
        {
            const ea::string& sceneFileName = scene_->GetFileName();
            if (sceneFileName.empty())
            {
                URHO3D_LOGERROR("Cannot find output directory for lightmaps: scene file name is undefined");
                return false;
            }

            settings_.incremental_.outputDirectory_ = ReplaceExtension(sceneFileName, "");
            if (settings_.incremental_.outputDirectory_ == sceneFileName)
            {
                URHO3D_LOGERROR("Cannot find output directory for lightmaps: scene file name has no extension");
                return false;
            }
        }

        settings_.incremental_.outputDirectory_ = AddTrailingSlash(settings_.incremental_.outputDirectory_);

        FileSystem* fs = context_->GetSubsystem<FileSystem>();
        if (!fs->CreateDir(settings_.incremental_.outputDirectory_))
        {
            URHO3D_LOGERROR("Cannot create output directory \"{}\" for lightmaps", settings_.incremental_.outputDirectory_);
            return false;
        }

        // Collect chunks
        collector_->LockScene(scene_, settings_.incremental_.chunkSize_);
        chunks_ = collector_->GetChunks();

        // Sort chunks
        if (!chunks_.empty())
        {
            const IntVector3 baseChunkIndex = ea::accumulate(chunks_.begin(), chunks_.end(), chunks_.front(), MinIntVector3);
            const auto compareSwizzled = [&](const IntVector3& lhs, const IntVector3& rhs)
            {
                return Swizzle(lhs, baseChunkIndex) < Swizzle(rhs, baseChunkIndex);
            };
            ea::sort(chunks_.begin(), chunks_.end(), compareSwizzled);
        }

        // Initialize GI data file
        auto gi = scene_->GetComponent<GlobalIllumination>();
        const ea::string giFileName = settings_.incremental_.outputDirectory_ + settings_.incremental_.giDataFileName_;
        const ea::string giFilePath = GetPath(giFileName);
        if (!fs->CreateDir(giFilePath))
        {
            URHO3D_LOGERROR("Cannot create output directory \"{}\" for GI data file", giFilePath);
            return false;
        }

        BinaryFile file(context_);
        if (!file.SaveFile(giFileName))
        {
            URHO3D_LOGERROR("Cannot allocate GI data file at \"{}\"", giFileName);
            return false;
        }
        gi->SetFileRef({ BinaryFile::GetTypeStatic(), GetResourceName(context_->GetSubsystem<ResourceCache>(), giFileName) });

        return true;
    }

    /// Generate charts and allocate light probes.
    void GenerateChartsAndUpdateScene()
    {
        auto cache = context_->GetSubsystem<ResourceCache>();
        auto fileSystem = context_->GetSubsystem<FileSystem>();

        numLightmapCharts_ = 0;

        for (const IntVector3& chunk : chunks_)
        {
            // Collect nodes for current chunks
            const ea::vector<Component*> uniqueGeometries = collector_->GetUniqueGeometries(chunk);
            const ea::vector<LightProbeGroup*> uniqueLightProbes = collector_->GetUniqueLightProbeGroups(chunk);

            // Generate charts
            const LightmapChartVector charts = GenerateLightmapCharts(
                uniqueGeometries, settings_.charting_, numLightmapCharts_);

            // Apply charts to scene
            ApplyLightmapCharts(charts);
            collector_->CommitGeometries(chunk);

            // Assign baked data files for light probe groups
            auto fileSystem = context_->GetSubsystem<FileSystem>();
            for (unsigned i = 0; i < uniqueLightProbes.size(); ++i)
            {
                LightProbeGroup* group = uniqueLightProbes[i];
                const ea::string fileName = GetLightProbeBakedDataFileName(chunk, i);
                const ea::string resourceName = GetResourceName(cache, fileName);
                fileSystem->CreateDirsRecursive(GetPath(fileName));
                group->SetBakedDataFileRef({ BinaryFile::GetTypeStatic(), resourceName });
            }

            // Update base index
            numLightmapCharts_ += charts.size();
        }

        // Update scene
        scene_->ResetLightmaps();
        for (unsigned i = 0; i < numLightmapCharts_; ++i)
        {
            const ea::string fileName = GetLightmapFileName(i);
            const ea::string resourceName = GetResourceName(cache, fileName);

            fileSystem->CreateDirsRecursive(GetPath(fileName));
            if (!fileSystem->FileExists(fileName))
            {
                Image placeholderImage(context_);
                placeholderImage.SetSize(1, 1, 4);
                placeholderImage.SetPixel(0, 0, Color::BLACK);
                placeholderImage.SaveFile(fileName);
            }

            if (resourceName.empty())
            {
                URHO3D_LOGWARNING("Cannot find resource name for lightmap \"{}\", absolute path is used", fileName);
                scene_->AddLightmap(fileName);
            }
            else
                scene_->AddLightmap(resourceName);
        }
    }

    /// Generate baking chunks.
    void GenerateBakingChunks()
    {
        for (const IntVector3& chunk : chunks_)
        {
            BakedSceneChunk bakedChunk = CreateBakedSceneChunk(context_, *collector_, chunk, settings_);
            cache_->StoreBakedChunk(chunk, ea::move(bakedChunk));
        }
    }

    /// Step direct light for charts.
    bool BakeDirectCharts(StopToken stopToken)
    {
        for (const IntVector3 chunk : chunks_)
        {
            const ea::shared_ptr<const BakedSceneChunk> bakedChunk = cache_->LoadBakedChunk(chunk);

            // Bake direct lighting
            for (unsigned i = 0; i < bakedChunk->lightmaps_.size(); ++i)
            {
                if (stopToken.IsStopped())
                    return false;

                const unsigned lightmapIndex = bakedChunk->lightmaps_[i];
                const LightmapChartGeometryBuffer& geometryBuffer = bakedChunk->geometryBuffers_[i];
                LightmapChartBakedDirect bakedDirect{ geometryBuffer.lightmapSize_ };

                // Bake emission
                BakeEmissionLight(bakedDirect, geometryBuffer,
                    settings_.emissionTracing_, settings_.properties_.emissionBrightness_);

                // Bake direct lights for charts
                for (const BakedLight& bakedLight : bakedChunk->bakedLights_)
                {
                    BakeDirectLightForCharts(bakedDirect, geometryBuffer, *bakedChunk->raytracerScene_,
                        bakedChunk->geometryBufferToRaytracer_, bakedLight, settings_.directChartTracing_);
                }

                // Store direct light
                cache_->StoreDirectLight(lightmapIndex, ea::move(bakedDirect));
            }
        }

        return true;
    }

    /// Bake indirect light, filter baked direct and indirect, bake direct light probes.
    bool BakeIndirectAndFilter(StopToken stopToken)
    {
        const unsigned numTexels = settings_.charting_.lightmapSize_ * settings_.charting_.lightmapSize_;
        ea::vector<Vector3> directFilterBuffer(numTexels);
        ea::vector<Vector4> indirectFilterBuffer(numTexels);
        LightProbeCollectionBakedData lightProbesBakedData;
        LightmapChartBakedIndirect bakedIndirect{ settings_.charting_.lightmapSize_ };

        for (const IntVector3 chunk : chunks_)
        {
            if (stopToken.IsStopped())
                return false;

            const ea::shared_ptr<const BakedSceneChunk> bakedChunk = cache_->LoadBakedChunk(chunk);

            // Collect required direct lightmaps
            ea::vector<ea::shared_ptr<const LightmapChartBakedDirect>> bakedDirectLightmapsRefs(numLightmapCharts_);
            ea::vector<const LightmapChartBakedDirect*> bakedDirectLightmaps(numLightmapCharts_);
            for (unsigned lightmapIndex : bakedChunk->requiredDirectLightmaps_)
            {
                bakedDirectLightmapsRefs[lightmapIndex] = cache_->LoadDirectLight(lightmapIndex);
                bakedDirectLightmaps[lightmapIndex] = bakedDirectLightmapsRefs[lightmapIndex].get();
            }

            // Allocate storage for light probes
            lightProbesBakedData.Resize(bakedChunk->lightProbesCollection_.GetNumProbes());

            // Bake indirect light for light probes
            BakeIndirectLightForLightProbes(lightProbesBakedData, bakedChunk->lightProbesCollection_,
                bakedDirectLightmaps, *bakedChunk->raytracerScene_, settings_.indirectProbesTracing_);

            // Build light probes mesh for fallback indirect
            TetrahedralMesh lightProbesMesh;
            lightProbesMesh.Define(bakedChunk->lightProbesCollection_.worldPositions_);

            // Bake indirect lighting for charts
            for (unsigned i = 0; i < bakedChunk->lightmaps_.size(); ++i)
            {
                if (stopToken.IsStopped())
                    return false;

                const unsigned lightmapIndex = bakedChunk->lightmaps_[i];
                const LightmapChartGeometryBuffer& geometryBuffer = bakedChunk->geometryBuffers_[i];
                const ea::shared_ptr<const LightmapChartBakedDirect> bakedDirect = cache_->LoadDirectLight(lightmapIndex);

                ea::fill(bakedIndirect.light_.begin(), bakedIndirect.light_.end(), Vector4::ZERO);

                // Bake indirect lights
                BakeIndirectLightForCharts(bakedIndirect, bakedDirectLightmaps,
                    geometryBuffer, lightProbesMesh, lightProbesBakedData,
                    *bakedChunk->raytracerScene_, bakedChunk->geometryBufferToRaytracer_,
                    settings_.indirectChartTracing_);

                // Filter direct and indirect
                bakedIndirect.NormalizeLight();

                if (settings_.directFilter_.kernelRadius_ > 0)
                {
                    FilterDirectLight(*bakedDirect, directFilterBuffer,
                        geometryBuffer, settings_.directFilter_, settings_.directChartTracing_.numTasks_);
                }

                if (settings_.indirectFilter_.kernelRadius_ > 0)
                {
                    FilterIndirectLight(bakedIndirect, indirectFilterBuffer,
                        geometryBuffer, settings_.indirectFilter_, settings_.indirectChartTracing_.numTasks_);
                }

                // Generate final images
                BakedLightmap bakedLightmap(settings_.charting_.lightmapSize_);
                for (unsigned i = 0; i < bakedLightmap.lightmap_.size(); ++i)
                {
                    const Vector3 directLight = static_cast<Vector3>(directFilterBuffer[i]);
                    const Vector3 indirectLight = static_cast<Vector3>(indirectFilterBuffer[i]);
                    bakedLightmap.lightmap_[i] = VectorMax(Vector3::ZERO, directLight);
                    bakedLightmap.lightmap_[i] += VectorMax(Vector3::ZERO, indirectLight);
                }

                // Store lightmap
                cache_->StoreLightmap(lightmapIndex, ea::move(bakedLightmap));
            }

            // Bake direct lights for light probes
            for (const BakedLight& bakedLight : bakedChunk->bakedLights_)
            {
                BakeDirectLightForLightProbes(lightProbesBakedData,
                    bakedChunk->lightProbesCollection_, *bakedChunk->raytracerScene_,
                    bakedLight, settings_.directProbesTracing_);
            }

            // Save light probes
            for (unsigned groupIndex = 0; groupIndex < bakedChunk->numUniqueLightProbes_; ++groupIndex)
            {
                const ea::string fileName = GetLightProbeBakedDataFileName(chunk, groupIndex);
                if (!LightProbeGroup::SaveLightProbesBakedData(context_, fileName,
                    bakedChunk->lightProbesCollection_, lightProbesBakedData, groupIndex))
                {
                    const ea::string groupName = groupIndex < bakedChunk->lightProbesCollection_.GetNumGroups()
                        ? bakedChunk->lightProbesCollection_.names_[groupIndex] : "";
                    URHO3D_LOGERROR("Cannot save light probes for group '{}' in chunk {}",
                        groupName, chunk.ToString());
                }
            }
        }
        return true;
    }

    // Stitch and save lightmaps.
    void StitchAndSaveImages()
    {
        const unsigned numTexels = settings_.charting_.lightmapSize_ * settings_.charting_.lightmapSize_;

        // Allocate stitching context
        LightmapStitchingContext stitchingContext = InitializeStitchingContext(
            context_, settings_.charting_.lightmapSize_, 4);

        // Allocate image to save
        ea::vector<Vector4> buffer(numTexels);

        auto lightmapImage = MakeShared<Image>(context_);
        if (!lightmapImage->SetSize(settings_.charting_.lightmapSize_, settings_.charting_.lightmapSize_, 4))
        {
            URHO3D_LOGERROR("Cannot allocate image for lightmap");
            return;
        }

        // Process all chunks
        for (const IntVector3 chunk : chunks_)
        {
            const ea::shared_ptr<const BakedSceneChunk> bakedChunk = cache_->LoadBakedChunk(chunk);
            for (unsigned i = 0; i < bakedChunk->lightmaps_.size(); ++i)
            {
                const unsigned lightmapIndex = bakedChunk->lightmaps_[i];
                const ea::shared_ptr<const BakedLightmap> bakedLightmap = cache_->LoadLightmap(lightmapIndex);
                const LightmapChartGeometryBuffer& geometryBuffer = bakedChunk->geometryBuffers_[i];

                // Stitch seams or just copy data to buffer
                if (settings_.stitching_.numIterations_ > 0 && !geometryBuffer.seams_.empty())
                {
                    SharedPtr<Model> seamsModel = CreateSeamsModel(context_, geometryBuffer.seams_);
                    StitchLightmapSeams(stitchingContext, bakedLightmap->lightmap_, buffer,
                        settings_.stitching_, seamsModel);
                }
                else
                {
                    for (unsigned i = 0; i < numTexels; ++i)
                        buffer[i] = Vector4(bakedLightmap->lightmap_[i], 1.0f);
                }

                // Generate image
                for (unsigned i = 0; i < numTexels; ++i)
                {
                    const unsigned x = i % geometryBuffer.lightmapSize_;
                    const unsigned y = i / geometryBuffer.lightmapSize_;

                    static const float multiplier = 1.0f / 2.0f;
                    Color color = static_cast<Color>(static_cast<Vector3>(buffer[i])).LinearToGamma();
                    color.r_ *= multiplier;
                    color.g_ *= multiplier;
                    color.b_ *= multiplier;
                    lightmapImage->SetPixel(x, y, color);
                }

                // Save image to destination folder
                const ea::string fileName = GetLightmapFileName(lightmapIndex);
                context_->GetSubsystem<FileSystem>()->CreateDirsRecursive(GetPath(fileName));
                lightmapImage->SaveFile(fileName);
            }
        }
    }

private:
    /// Return lightmap file name.
    ea::string GetLightmapFileName(unsigned lightmapIndex)
    {
        ea::string fileName;
        fileName += settings_.incremental_.outputDirectory_;
        fileName += Format(settings_.incremental_.lightmapNameFormat_, lightmapIndex);
        return fileName;
    }

    /// Return light probe group baked data file.
    ea::string GetLightProbeBakedDataFileName(const IntVector3& chunk, unsigned index)
    {
        ea::string fileName;
        fileName += settings_.incremental_.outputDirectory_;
        fileName += Format(settings_.incremental_.lightProbeGroupNameFormat_, chunk.x_, chunk.y_, chunk.z_, index);
        return fileName;
    }

    /// Settings for light baking.
    LightBakingSettings settings_;

    /// Context.
    Context* context_{};
    /// Scene.
    Scene* scene_{};
    /// Scene collector.
    BakedSceneCollector* collector_{};
    /// Lightmap cache.
    BakedLightCache* cache_{};
    /// List of all chunks.
    ea::vector<IntVector3> chunks_;
    /// Number of lightmap charts.
    unsigned numLightmapCharts_{};
};

IncrementalLightBaker::~IncrementalLightBaker()
{
}

bool IncrementalLightBaker::Initialize(const LightBakingSettings& settings,
    Scene* scene, BakedSceneCollector* collector, BakedLightCache* cache)
{
    impl_ = ea::make_unique<Impl>(settings, scene, collector, cache);
    return impl_->Initialize();
}

void IncrementalLightBaker::ProcessScene()
{
    impl_->GenerateChartsAndUpdateScene();
    impl_->GenerateBakingChunks();
}

bool IncrementalLightBaker::Bake(StopToken stopToken)
{
    if (!impl_->BakeDirectCharts(stopToken))
        return false;

    if (!impl_->BakeIndirectAndFilter(stopToken))
        return false;

    return true;
}

void IncrementalLightBaker::CommitScene()
{
    impl_->StitchAndSaveImages();
}

}
