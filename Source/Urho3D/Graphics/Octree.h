//
// Copyright (c) 2008-2020 the Urho3D project.
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

#pragma once

#include "../Core/Mutex.h"
#include "../Graphics/Drawable.h"
#include "../Graphics/OctreeQuery.h"

namespace Urho3D
{

class Octree;
class Zone;

static const int NUM_OCTANTS = 8;
static const unsigned ROOT_INDEX = M_MAX_UNSIGNED;

/// %Octree octant.
/// @nobind
class URHO3D_API Octant
{
public:
    /// Construct.
    Octant(const BoundingBox& box, unsigned level, Octant* parent, Octree* octree, unsigned index = ROOT_INDEX);
    /// Destruct. Move drawables to root if available (detach if not) and free child octants.
    virtual ~Octant();

    /// Return or create a child octant.
    Octant* GetOrCreateChild(unsigned index);
    /// Delete child octant.
    void DeleteChild(unsigned index);
    /// Insert a drawable object by checking for fit recursively.
    void InsertDrawable(Drawable* drawable);
    /// Check if a drawable object fits.
    bool CheckDrawableFit(const BoundingBox& box) const;

    /// Add a drawable object to this octant.
    void AddDrawable(Drawable* drawable)
    {
        drawable->SetOctant(this);
        drawables_.push_back(drawable);
        IncDrawableCount();
    }

    /// Remove a drawable object from this octant.
    void RemoveDrawable(Drawable* drawable, bool resetOctant = true)
    {
        auto it = drawables_.find(drawable);
        if (it != drawables_.end())
        {
            drawables_.erase(it);
            if (resetOctant)
                drawable->SetOctant(nullptr);
            DecDrawableCount();
        }
    }

    /// Return world-space bounding box.
    /// @property
    const BoundingBox& GetWorldBoundingBox() const { return worldBoundingBox_; }

    /// Return bounding box used for fitting drawable objects.
    const BoundingBox& GetCullingBox() const { return cullingBox_; }

    /// Return subdivision level.
    unsigned GetLevel() const { return level_; }

    /// Return parent octant.
    Octant* GetParent() const { return parent_; }

    /// Return octree.
    Octree* GetOctree() const { return octree_; }

    /// Return number of drawables.
    unsigned GetNumDrawables() const { return numDrawables_; }

    /// Return true if there are no drawable objects in this octant and child octants.
    bool IsEmpty() { return numDrawables_ == 0; }

    /// Set size for the root octant. If octree is not empty, drawable objects will be temporarily moved to the root.
    void SetRootSize(const BoundingBox& box);
    /// Reset octree pointer recursively. Called when the whole octree is being destroyed.
    void ResetOctree();
    /// Draw bounds to the debug graphics recursively.
    /// @nobind
    void DrawDebugGeometry(DebugRenderer* debug, bool depthTest);

    /// Return drawable objects by a query, called internally.
    void GetDrawablesInternal(OctreeQuery& query, bool inside) const;
    /// Return drawable objects by a ray query, called internally.
    void GetDrawablesInternal(RayOctreeQuery& query) const;
    /// Return drawable objects only for a threaded ray query, called internally.
    void GetDrawablesOnlyInternal(RayOctreeQuery& query, ea::vector<Drawable*>& drawables) const;

protected:
    /// Initialize bounding box.
    void Initialize(const BoundingBox& box);

    /// Increase drawable object count recursively.
    void IncDrawableCount()
    {
        ++numDrawables_;
        if (parent_)
            parent_->IncDrawableCount();
    }

    /// Decrease drawable object count recursively and remove octant if it becomes empty.
    void DecDrawableCount()
    {
        Octant* parent = parent_;

        --numDrawables_;
        if (!numDrawables_)
        {
            if (parent)
                parent->DeleteChild(index_);
        }

        if (parent)
            parent->DecDrawableCount();
    }

    /// World bounding box.
    BoundingBox worldBoundingBox_;
    /// Bounding box used for drawable object fitting.
    BoundingBox cullingBox_;
    /// Drawable objects.
    ea::vector<Drawable*> drawables_;
    /// Child octants.
    Octant* children_[NUM_OCTANTS]{};
    /// World bounding box center.
    Vector3 center_;
    /// World bounding box half size.
    Vector3 halfSize_;
    /// Subdivision level.
    unsigned level_{};
    /// Number of drawable objects in this octant and child octants.
    unsigned numDrawables_{};
    /// Parent octant.
    Octant* parent_{};
    /// Octree root.
    Octree* octree_{};
    /// Octant index relative to its siblings or ROOT_INDEX for root octant.
    unsigned index_{};
};

/// Acceleration structure for zone search.
class URHO3D_API ZoneLookupIndex
{
public:
    explicit ZoneLookupIndex(Context* context);

    /// @name Manage zones
    /// @{
    void AddZone(Zone* zone);
    void UpdateZone(Zone* zone);
    void RemoveZone(Zone* zone);
    /// @}

    /// Commit all updates. Called on every frame.
    void Commit();

    /// Query zone for given position and mask.
    CachedDrawableZone QueryZone(const Vector3& position, unsigned zoneMask) const;
    /// Return background zone.
    Zone* GetBackgroundZone() const;

private:
    /// Cached zone parameters.
    struct ZoneData
    {
        /// Local bounding box.
        BoundingBox boundingBox_;
        /// Inverse world transform.
        Matrix3x4 inverseWorldTransform_;
        /// Zone mask.
        unsigned zoneMask_{};
    };

    /// Default zone.
    Zone* defaultZone_{};
    /// Zones.
    ea::vector<Zone*> zones_;
    /// Cached zone parameters.
    ea::vector<ZoneData> zonesData_;
    /// Whether zones are dirty.
    bool zonesDirty_{};
};

/// %Octree component. Should be added only to the root scene node.
class URHO3D_API Octree : public Component
{
    URHO3D_OBJECT(Octree, Component);

public:
    /// Construct.
    explicit Octree(Context* context);
    /// Destruct.
    ~Octree() override;
    /// Register object factory.
    /// @nobind
    static void RegisterObject(Context* context);

    /// Visualize the component as debug geometry.
    void DrawDebugGeometry(DebugRenderer* debug, bool depthTest) override;

    /// Set size and maximum subdivision levels. If octree is not empty, drawable objects will be temporarily moved to the root.
    void SetSize(const BoundingBox& box, unsigned numLevels);
    /// Update and reinsert drawable objects.
    void Update(const FrameInfo& frame);
    /// Add a drawable manually.
    void AddManualDrawable(Drawable* drawable);
    /// Remove a manually added drawable.
    void RemoveManualDrawable(Drawable* drawable);

    /// Add drawable is added to octree. For internal use only.
    void AddDrawable(Drawable* drawable);
    /// Remove drawable from octree. For internal use only.
    void RemoveDrawable(Drawable* drawable, Octant* octant);
    /// Notify Octree that zone parameters changed. For internal use only.
    void MarkZoneDirty(Zone* zone);

    /// Return drawable objects by a query.
    /// @nobind
    void GetDrawables(OctreeQuery& query) const;
    /// Return drawable objects by a ray query.
    void Raycast(RayOctreeQuery& query) const;
    /// Return the closest drawable object by a ray query.
    void RaycastSingle(RayOctreeQuery& query) const;
    /// Return best zone for drawable.
    CachedDrawableZone QueryZone(Drawable* drawable) const;
    /// Return best zone for drawable with given center in world space and zone mask.
    CachedDrawableZone QueryZone(const Vector3& drawablePosition, unsigned zoneMask) const;
    /// Return background zone (arbitrary zone with 0 priority or lower). Zones with positive priority are ignored.
    Zone* GetBackgroundZone() const;

    /// Return root octant.
    const Octant* GetRootOctant() const { return &rootOctant_; }

    /// Return root octant.
    Octant* GetRootOctant() { return &rootOctant_; }

    /// Return subdivision levels.
    /// @property
    unsigned GetNumLevels() const { return numLevels_; }

    /// Return all drawables in all octants.
    const ea::vector<Drawable*>& GetAllDrawables() const { return drawables_; }

    /// Mark drawable object as requiring an update and a reinsertion.
    void QueueUpdate(Drawable* drawable);
    /// Cancel drawable object's update.
    void CancelUpdate(Drawable* drawable);
    /// Visualize the component as debug geometry.
    void DrawDebugGeometry(bool depthTest);

private:
    /// Handle render update in case of headless execution.
    void HandleRenderUpdate(StringHash eventType, VariantMap& eventData);
    /// Update octree size.
    void UpdateOctreeSize() { SetSize(worldBoundingBox_, numLevels_); }

    /// Root octant.
    Octant rootOctant_;
    /// Drawable objects that require update.
    ea::vector<Drawable*> drawableUpdates_;
    /// Drawable objects that were inserted during threaded update phase.
    ea::vector<Drawable*> threadedDrawableUpdates_;
    /// All Drawable objects.
    ea::vector<Drawable*> drawables_;
    /// Mutex for octree reinsertions.
    Mutex octreeMutex_;
    /// Ray query temporary list of drawables.
    mutable ea::vector<Drawable*> rayQueryDrawables_;
    /// Subdivision level.
    unsigned numLevels_;
    /// World bounding box.
    BoundingBox worldBoundingBox_;
    /// Zones.
    ZoneLookupIndex zones_;
};

}
