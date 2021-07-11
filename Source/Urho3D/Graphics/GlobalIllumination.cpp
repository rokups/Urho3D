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

#include "../Precompiled.h"

#include "../Graphics/GlobalIllumination.h"

#include "../Core/Context.h"
#include "../Graphics/DebugRenderer.h"
#include "../IO/ArchiveSerialization.h"
#include "../IO/BinaryArchive.h"
#include "../IO/Log.h"
#include "../Resource/ResourceCache.h"
#include "../Scene/Scene.h"

namespace Urho3D
{

extern const char* SUBSYSTEM_CATEGORY;

GlobalIllumination::GlobalIllumination(Context* context) :
    Component(context)
{
}

GlobalIllumination::~GlobalIllumination() = default;

void GlobalIllumination::RegisterObject(Context* context)
{
    context->RegisterFactory<GlobalIllumination>(SUBSYSTEM_CATEGORY);

    URHO3D_ATTRIBUTE("Emission Brightness", float, emissionBrightness_, 1.0f, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Data File", GetFileRef, SetFileRef, ResourceRef, ResourceRef{ BinaryFile::GetTypeStatic() }, AM_DEFAULT | AM_NOEDIT);
}

void GlobalIllumination::DrawDebugGeometry(DebugRenderer* debug, bool depthTest)
{
    static thread_local ea::vector<ea::pair<unsigned, unsigned>> edges;
    lightProbesMesh_.CollectEdges(edges);

    for (const auto& edge : edges)
    {
        const Vector3& startPos = lightProbesMesh_.vertices_[edge.first];
        const Vector3& endPos = lightProbesMesh_.vertices_[edge.second];
        debug->AddLine(startPos, endPos, Color::YELLOW);
    }

    for (unsigned ignoredVertex : lightProbesMesh_.ignoredVertices_)
    {
        const Vector3& position = lightProbesMesh_.vertices_[ignoredVertex];
        const BoundingBox boundingBox{ position - Vector3::ONE * 0.1f, position + Vector3::ONE * 0.1f };
        debug->AddBoundingBox(boundingBox, Color::RED);
    }

    for (const auto& highlightEdge : lightProbesMesh_.debugHighlightEdges_)
    {
        const Vector3& startPos = lightProbesMesh_.vertices_[highlightEdge.first];
        const Vector3& endPos = lightProbesMesh_.vertices_[highlightEdge.second];
        debug->AddLine(startPos, endPos, Color::RED);
    }
}

void GlobalIllumination::ResetLightProbes()
{
    lightProbesBakedData_.Clear();
    lightProbesMesh_ = {};
}

void GlobalIllumination::CompileLightProbes()
{
    ResetLightProbes();

    // Collect light probes
    LightProbeCollection collection;
    LightProbeGroup::CollectLightProbes(GetScene(), collection, &lightProbesBakedData_, true /*reload*/);
    if (collection.Empty())
        return;

    // Add padding to avoid vertex collision
    lightProbesMesh_.Define(collection.worldPositions_);

    // Store in file
    auto cache = context_->GetSubsystem<ResourceCache>();
    auto file = cache->GetTempResource<BinaryFile>(fileRef_.name_);
    if (!file)
    {
        URHO3D_LOGERROR("Cannot save Global Illumination data: output file doesn't exist");
        return;
    }

    file->Clear();
    BinaryOutputArchive archive = file->AsOutputArchive();

    if (!SerializeData(archive))
    {
        URHO3D_LOGERROR("Cannot save Global Illumination data: serialization failed");
        return;
    }

    if (!file->SaveFile(file->GetNativeFileName()))
    {
        URHO3D_LOGERROR("Cannot save Global Illumination data: cannot save file");
        return;
    }
}

SphericalHarmonicsDot9 GlobalIllumination::SampleAmbientSH(const Vector3& position, unsigned& hint) const
{
    return lightProbesMesh_.Sample(lightProbesBakedData_.sphericalHarmonics_, position, hint);
}

Vector3 GlobalIllumination::SampleAverageAmbient(const Vector3& position, unsigned& hint) const
{
    return lightProbesMesh_.Sample(lightProbesBakedData_.ambient_, position, hint);
}

void GlobalIllumination::SetFileRef(const ResourceRef& fileRef)
{
    if (fileRef_ != fileRef)
    {
        fileRef_ = fileRef;
        ReloadData();
    }
}

ResourceRef GlobalIllumination::GetFileRef() const
{
    return fileRef_;
}

bool GlobalIllumination::SerializeData(Archive& archive)
{
    if (ArchiveBlock block = archive.OpenUnorderedBlock("LightProbes"))
    {
        static const unsigned currentVersion = 2;
        const unsigned version = archive.SerializeVersion(currentVersion);
        if (version == currentVersion)
        {
            SerializeValue(archive, "Mesh", lightProbesMesh_);
            SerializeValue(archive, "Data", lightProbesBakedData_);
            return true;
        }
    }
    return false;
}

void GlobalIllumination::ReloadData()
{
    auto cache = context_->GetSubsystem<ResourceCache>();
    auto file = cache->GetTempResource<BinaryFile>(fileRef_.name_);

    bool success = false;
    if (file)
    {
        BinaryInputArchive archive = file->AsInputArchive();
        if (SerializeData(archive))
            success = true;
    }

    if (!success)
    {
        lightProbesMesh_ = {};
        lightProbesBakedData_.Clear();
    }
}

}
