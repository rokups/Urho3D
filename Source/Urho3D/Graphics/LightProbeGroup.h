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

#include "../Graphics/Drawable.h"
#include "../Math/BoundingBox.h"
#include "../Math/SphericalHarmonics.h"
#include "../Scene/Component.h"
#include "../Resource/BinaryFile.h"

namespace Urho3D
{

class LightProbeGroup;

/// Light probe description.
struct LightProbe
{
    /// Position in local space of light probe group.
    Vector3 position_;
};

/// Serialize light probe.
bool SerializeValue(Archive& archive, const char* name, LightProbe& value);

/// Vector of light probes.
using LightProbeVector = ea::vector<LightProbe>;

/// Light probe baked data.
struct LightProbeCollectionBakedData
{
    /// Incoming light baked into spherical harmonics.
    ea::vector<SphericalHarmonicsDot9> sphericalHarmonics_;
    /// Baked ambient light.
    ea::vector<Vector3> ambient_;

    /// Return whether the collection is empty.
    bool Empty() const { return sphericalHarmonics_.empty(); }

    /// Return total size.
    unsigned Size() const { return sphericalHarmonics_.size(); }

    /// Resize collection.
    void Resize(unsigned size)
    {
        sphericalHarmonics_.resize(size);
        ambient_.resize(size);
    }

    /// Clear collection.
    void Clear()
    {
        sphericalHarmonics_.clear();
        ambient_.clear();
    }
};

/// Serialize light probe baked data.
bool SerializeValue(Archive& archive, const char* name, LightProbeCollectionBakedData& value);

/// Light probes from multiple light probe groups.
struct LightProbeCollection
{
    /// World-space positions of light probes.
    ea::vector<Vector3> worldPositions_;

    /// First light probe owned by corresponding group.
    ea::vector<unsigned> offsets_;
    /// Number of light probes owned by corresponding group.
    ea::vector<unsigned> counts_;
    /// Group names.
    ea::vector<ea::string> names_;

    /// Return whether the collection is empty.
    bool Empty() const { return worldPositions_.empty(); }

    /// Return total number of probes.
    unsigned GetNumProbes() const { return worldPositions_.size(); }

    /// Return number of groups.
    unsigned GetNumGroups() const { return offsets_.size(); }

    /// Calculate padded bounding box.
    BoundingBox CalculateBoundingBox(const Vector3& padding = Vector3::ZERO)
    {
        BoundingBox boundingBox(worldPositions_.data(), worldPositions_.size());
        boundingBox.min_ -= padding;
        boundingBox.max_ += padding;
        return boundingBox;
    }

    /// Clear collection.
    void Clear()
    {
        worldPositions_.clear();
        offsets_.clear();
        counts_.clear();
        names_.clear();
    }
};

/// Light probe group.
class URHO3D_API LightProbeGroup : public Component
{
    URHO3D_OBJECT(LightProbeGroup, Component);

public:
    /// Auto placement limit: max grid size in one dimension.
    static const unsigned MaxAutoGridSize = 1024;
    /// Auto placement limit: max total number of probes generated.
    static const unsigned MaxAutoProbes = 65536;

    /// Construct.
    explicit LightProbeGroup(Context* context);
    /// Destruct.
    ~LightProbeGroup() override;
    /// Register object factory. Drawable must be registered first.
    static void RegisterObject(Context* context);
    /// Visualize the component as debug geometry.
    void DrawDebugGeometry(DebugRenderer* debug, bool depthTest) override;

    /// Collect all light probes from specified groups.
    static void CollectLightProbes(const ea::vector<LightProbeGroup*>& lightProbeGroups,
        LightProbeCollection& collection, LightProbeCollectionBakedData* bakedData, bool reload = false);
    /// Collect all light probes from all enabled groups in the scene.
    static void CollectLightProbes(Scene* scene,
        LightProbeCollection& collection, LightProbeCollectionBakedData* bakedData, bool reload = false);
    /// Save light probes baked data for specific element. Return false if failed.
    static bool SaveLightProbesBakedData(Context* context, const ea::string& fileName,
        const LightProbeCollection& collection, const LightProbeCollectionBakedData& bakedData, unsigned index);

    /// Return bounding box in local space.
    BoundingBox GetLocalBoundingBox() const { return localBoundingBox_; }
    /// Return bounding box in world space.
    BoundingBox GetWorldBoundingBox() const;

    /// Arrange light probes in scale.x*scale.y*scale.z volume around the node.
    void ArrangeLightProbesInVolume();
    /// Reload baked light probes data.
    void ReloadBakedData();

    /// Attributes
    /// @{
    void SetAutoPlacementEnabled(bool enabled);
    bool GetAutoPlacementEnabled() const { return autoPlacementEnabled_; }
    void SetAutoPlacementStep(float step);
    float GetAutoPlacementStep() const { return autoPlacementStep_; }
    void SetLightMask(unsigned lightMask) { lightMask_ = lightMask; }
    unsigned GetLightMask() const { return lightMask_; }
    void SetZoneMask(unsigned zoneMask) { zoneMask_ = zoneMask; }
    unsigned GetZoneMask() const { return zoneMask_; }
    /// @}

    /// Set light probes.
    void SetLightProbes(const LightProbeVector& lightProbes);
    /// Return light probes.
    const LightProbeVector& GetLightProbes() const { return lightProbes_; }

    /// Serialize light probes.
    void SerializeLightProbes(Archive& archive);
    /// Set serialized light probes.
    void SetSerializedLightProbes(const ea::string& data);
    /// Return serialized light probes.
    ea::string GetSerializedLightProbes() const;

    /// Serialize baked data.
    static bool SerializeBakedData(Archive& archive, LightProbeCollectionBakedData& bakedData);
    /// Set reference on file with baked data.
    void SetBakedDataFileRef(const ResourceRef& fileRef);
    /// Return reference on file with baked data.
    ResourceRef GetBakedDataFileRef() const;

protected:
    /// Handle scene node being assigned at creation.
    void OnNodeSet(Node* node) override;
    /// Handle scene node transform dirtied.
    void OnMarkedDirty(Node* node) override;
    /// Update local bounding box.
    void UpdateLocalBoundingBox();
    /// Update baked data.
    void UpdateBakedData();

    /// Light probes.
    LightProbeVector lightProbes_;
    /// Bounding box in local space.
    BoundingBox localBoundingBox_;

    /// Light mask of light probe group.
    unsigned lightMask_{ DEFAULT_LIGHTMASK };
    /// Zone mask of light probe group.
    unsigned zoneMask_{ DEFAULT_ZONEMASK };
    /// Whether the auto placement is enabled.
    bool autoPlacementEnabled_{ true };
    /// Automatic placement step.
    float autoPlacementStep_{ 1.0f };
    /// Last node scale used during auto placement.
    Vector3 lastNodeScale_;

    /// Reference on file with baked data.
    ResourceRef bakedDataRef_{ BinaryFile::GetTypeStatic() };
    /// Whether the baked data is dirty.
    bool bakedDataDirty_{};
    /// Baked data.
    LightProbeCollectionBakedData bakedData_;
};

}
