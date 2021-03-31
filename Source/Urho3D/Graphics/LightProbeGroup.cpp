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

#include "../Graphics/LightProbeGroup.h"

#include "../Core/Context.h"
#include "../IO/ArchiveSerialization.h"
#include "../IO/BinaryArchive.h"
#include "../IO/Log.h"
#include "../Graphics/DebugRenderer.h"
#include "../Resource/ResourceCache.h"
#include "../Scene/Node.h"
#include "../Scene/Scene.h"

namespace Urho3D
{

extern const char* SCENE_CATEGORY;

bool SerializeValue(Archive& archive, const char* name, LightProbe& value)
{
    if (ArchiveBlock block = archive.OpenUnorderedBlock(name))
    {
        SerializeValue(archive, "Position", value.position_);
        return true;
    }
    return false;
}

bool SerializeValue(Archive& archive, const char* name, LightProbeCollectionBakedData& value)
{
    if (ArchiveBlock block = archive.OpenUnorderedBlock(name))
    {
        static const unsigned currentVersion = 1;
        const unsigned version = archive.SerializeVersion(currentVersion);
        if (version == currentVersion)
        {
            SerializeVector(archive, "SH9", "Element", value.sphericalHarmonics_);

            // Generate ambient if loading
            if (archive.IsInput())
            {
                const unsigned numLightProbes = value.Size();
                value.ambient_.resize(numLightProbes);
                for (unsigned i = 0; i < numLightProbes; ++i)
                    value.ambient_[i] = value.sphericalHarmonics_[i].GetDebugColor().ToVector3();
            }

            return true;
        }
    }
    return false;
}

LightProbeGroup::LightProbeGroup(Context* context) :
    Component(context)
{
}

LightProbeGroup::~LightProbeGroup() = default;

void LightProbeGroup::RegisterObject(Context* context)
{
    context->RegisterFactory<LightProbeGroup>(SCENE_CATEGORY);

    URHO3D_ACCESSOR_ATTRIBUTE("Is Enabled", IsEnabled, SetEnabled, bool, true, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Light Mask", unsigned, lightMask_, DEFAULT_LIGHTMASK, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Zone Mask", unsigned, zoneMask_, DEFAULT_ZONEMASK, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Auto Placement", GetAutoPlacementEnabled, SetAutoPlacementEnabled, bool, true, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Auto Placement Step", GetAutoPlacementStep, SetAutoPlacementStep, float, 1.0f, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Local Bounding Box Min", Vector3, localBoundingBox_.min_, Vector3::ZERO, AM_DEFAULT | AM_NOEDIT);
    URHO3D_ATTRIBUTE("Local Bounding Box Max", Vector3, localBoundingBox_.max_, Vector3::ZERO, AM_DEFAULT | AM_NOEDIT);
    URHO3D_ACCESSOR_ATTRIBUTE("Serialized Light Probes", GetSerializedLightProbes, SetSerializedLightProbes, ea::string, EMPTY_STRING, AM_DEFAULT | AM_NOEDIT);
    URHO3D_ACCESSOR_ATTRIBUTE("Baked Data File", GetBakedDataFileRef, SetBakedDataFileRef, ResourceRef, ResourceRef{ BinaryFile::GetTypeStatic() }, AM_DEFAULT | AM_NOEDIT);
}

void LightProbeGroup::DrawDebugGeometry(DebugRenderer* debug, bool depthTest)
{
    UpdateBakedData();

    const BoundingBox boundingBox{ -Vector3::ONE * 0.5f, Vector3::ONE * 0.5f };
    debug->AddBoundingBox(boundingBox, node_->GetWorldTransform(), Color::GREEN);

    for (unsigned i = 0; i < lightProbes_.size(); ++i)
    {
        const LightProbe& probe = lightProbes_[i];
        const Vector3 worldPosition = node_->LocalToWorld(probe.position_);
        debug->AddSphere(Sphere(worldPosition, 0.1f), static_cast<Color>(bakedData_.ambient_[i]));
    }
}

BoundingBox LightProbeGroup::GetWorldBoundingBox() const
{
    return localBoundingBox_.Transformed(node_->GetWorldTransform());
}

void LightProbeGroup::CollectLightProbes(const ea::vector<LightProbeGroup*>& lightProbeGroups,
    LightProbeCollection& collection, LightProbeCollectionBakedData* bakedData, bool reload)
{
    // Initialize offset according to current state of collection
    unsigned offset = collection.GetNumProbes();

    for (LightProbeGroup* group : lightProbeGroups)
    {
        // Ensure that baked data is up to data
        if (bakedData)
        {
            if (reload)
                group->ReloadBakedData();
            group->UpdateBakedData();
        }

        // Store metadata
        Node* node = group->GetNode();
        const LightProbeVector& probes = group->GetLightProbes();

        collection.offsets_.push_back(offset);
        collection.counts_.push_back(probes.size());
        collection.names_.push_back(node->GetName());
        offset += probes.size();

        // Store light probes data
        for (const LightProbe& probe : probes)
        {
            const Vector3 worldPosition = node->LocalToWorld(probe.position_);
            collection.worldPositions_.push_back(worldPosition);
        }

        // Store baked data
        if (bakedData)
        {
            bakedData->sphericalHarmonics_.append(group->bakedData_.sphericalHarmonics_);
            bakedData->ambient_.append(group->bakedData_.ambient_);
        }
    }
}

void LightProbeGroup::CollectLightProbes(Scene* scene,
    LightProbeCollection& collection, LightProbeCollectionBakedData* bakedData, bool reload)
{
    ea::vector<LightProbeGroup*> lightProbeGroups;
    scene->GetComponents(lightProbeGroups, true);

    const auto isNotEnabled = [](const LightProbeGroup* lightProbeGroup) { return !lightProbeGroup->IsEnabledEffective(); };
    lightProbeGroups.erase(ea::remove_if(lightProbeGroups.begin(), lightProbeGroups.end(), isNotEnabled), lightProbeGroups.end());

    CollectLightProbes(lightProbeGroups, collection, bakedData, reload);
}

bool LightProbeGroup::SaveLightProbesBakedData(Context* context, const ea::string& fileName,
    const LightProbeCollection& collection, const LightProbeCollectionBakedData& bakedData, unsigned index)
{
    if (index >= collection.GetNumGroups())
        return false;

    const unsigned offset = collection.offsets_[index];
    const unsigned count = collection.counts_[index];
    if (offset + count > bakedData.Size())
        return false;

    LightProbeCollectionBakedData copy;

    auto sphericalHarmonicsBegin = bakedData.sphericalHarmonics_.begin() + offset;
    auto ambientBegin = bakedData.ambient_.begin() + offset;
    copy.sphericalHarmonics_.assign(sphericalHarmonicsBegin, sphericalHarmonicsBegin + count);
    copy.ambient_.assign(ambientBegin, ambientBegin + count);

    BinaryFile bakedDataFile(context);

    BinaryOutputArchive archive = bakedDataFile.AsOutputArchive();
    if (!SerializeBakedData(archive, copy))
        return false;

    if (!bakedDataFile.SaveFile(fileName))
        return false;

    return true;
}

void LightProbeGroup::ArrangeLightProbesInVolume()
{
    bakedDataDirty_ = true; // Reset baked data every time light probes change
    lightProbes_.clear();
    if (autoPlacementStep_ <= M_LARGE_EPSILON)
        return;

    const Vector3 volumeSize = VectorAbs(node_->GetScale());
    const IntVector3 gridSize = VectorMax(IntVector3::ONE * 2,
        IntVector3::ONE + VectorRoundToInt(volumeSize / autoPlacementStep_));
    const int maxGridSize = ea::max({ gridSize.x_, gridSize.y_, gridSize.z_ });

    // Integer multiplication overflow is undefined behaviour and should be treated carefully.
    if (maxGridSize >= static_cast<int>(MaxAutoGridSize)
        || gridSize.x_ * gridSize.y_ * gridSize.z_ >= static_cast<int>(MaxAutoProbes))
    {
        URHO3D_LOGERROR("Automatic Light Probe Grid is too big");
        return;
    }

    // Fill volume with probes
    const Vector3 gridStep = Vector3::ONE / static_cast<Vector3>(gridSize - IntVector3::ONE);
    IntVector3 index;
    for (index.z_ = 0; index.z_ < gridSize.z_; ++index.z_)
    {
        for (index.y_ = 0; index.y_ < gridSize.y_; ++index.y_)
        {
            for (index.x_ = 0; index.x_ < gridSize.x_; ++index.x_)
            {
                const Vector3 localPosition = -Vector3::ONE / 2 + static_cast<Vector3>(index) * gridStep;
                lightProbes_.push_back(LightProbe{ localPosition });
            }
        }
    }

    localBoundingBox_ = { -Vector3::ONE / 2, Vector3::ONE / 2 };
}

void LightProbeGroup::ReloadBakedData()
{
    bakedDataDirty_ = true;
    UpdateBakedData();
}

void LightProbeGroup::SetAutoPlacementEnabled(bool enabled)
{
    autoPlacementEnabled_ = enabled;
    if (autoPlacementEnabled_)
        ArrangeLightProbesInVolume();
}

void LightProbeGroup::SetAutoPlacementStep(float step)
{
    autoPlacementStep_ = step;
    if (autoPlacementEnabled_)
        ArrangeLightProbesInVolume();
}

void LightProbeGroup::SetLightProbes(const LightProbeVector& lightProbes)
{
    lightProbes_ = lightProbes;
    bakedDataDirty_ = true; // Reset baked data every time light probes change
    UpdateLocalBoundingBox();
}

void LightProbeGroup::SerializeLightProbes(Archive& archive)
{
    if (ArchiveBlock block = archive.OpenUnorderedBlock("LightProbesData"))
    {
        static const unsigned currentVersion = 2;
        const unsigned version = archive.SerializeVersion(currentVersion);
        if (version == currentVersion)
        {
            SerializeVector(archive, "LightProbes", "LightProbe", lightProbes_);
        }
    }
}

void LightProbeGroup::SetSerializedLightProbes(const ea::string& data)
{
    VectorBuffer buffer(DecodeBase64(data));
    BinaryInputArchive archive(context_, buffer);
    SerializeLightProbes(archive);
    bakedDataDirty_ = true; // Reset baked data every time light probes change
}

ea::string LightProbeGroup::GetSerializedLightProbes() const
{
    VectorBuffer buffer;
    BinaryOutputArchive archive(context_, buffer);
    const_cast<LightProbeGroup*>(this)->SerializeLightProbes(archive);
    return EncodeBase64(buffer.GetBuffer());
}

bool LightProbeGroup::SerializeBakedData(Archive& archive, LightProbeCollectionBakedData& bakedData)
{
    return SerializeValue(archive, "LightProbesBakedData", bakedData);
}

void LightProbeGroup::SetBakedDataFileRef(const ResourceRef& fileRef)
{
    if (bakedDataRef_ != fileRef)
    {
        bakedDataDirty_ = true;
        bakedDataRef_ = fileRef;
    }
}

ResourceRef LightProbeGroup::GetBakedDataFileRef() const
{
    return bakedDataRef_;
}

void LightProbeGroup::OnNodeSet(Node* node)
{
    if (node)
        node->AddListener(this);
}

void LightProbeGroup::OnMarkedDirty(Node* node)
{
    if (autoPlacementEnabled_ && lastNodeScale_ != node->GetScale())
    {
        lastNodeScale_ = node->GetScale();
        ArrangeLightProbesInVolume();
    }
}

void LightProbeGroup::UpdateLocalBoundingBox()
{
    localBoundingBox_.Clear();
    for (const LightProbe& probe : lightProbes_)
        localBoundingBox_.Merge(probe.position_);
}

void LightProbeGroup::UpdateBakedData()
{
    if (!bakedDataDirty_)
        return;

    bakedDataDirty_ = false;
    auto cache = context_->GetSubsystem<ResourceCache>();
    auto bakedDataFile = cache->GetTempResource<BinaryFile>(bakedDataRef_.name_);

    // Try to load from file
    bool success = false;

    if (bakedDataFile)
    {
        BinaryInputArchive archive = bakedDataFile->AsInputArchive();
        if (SerializeBakedData(archive, bakedData_))
        {
            if (bakedData_.sphericalHarmonics_.size() == lightProbes_.size())
                success = true;
        }
    }

    // Reset to default if failed
    if (!success)
    {
        bakedData_.Resize(lightProbes_.size());
        for (unsigned i = 0; i < lightProbes_.size(); ++i)
        {
            bakedData_.sphericalHarmonics_[i] = SphericalHarmonicsDot9::ZERO;
            bakedData_.ambient_[i] = Vector3::ZERO;
        }
    }
}

}
