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

#include "../Precompiled.h"

#include <EASTL/sort.h>

#include "../Core/Context.h"
#include "../Core/CoreEvents.h"
#include "../Core/Profiler.h"
#include "../Core/Thread.h"
#include "../Core/WorkQueue.h"
#include "../Graphics/DebugRenderer.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Octree.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/Zone.h"
#include "../IO/Log.h"
#include "../Scene/Scene.h"
#include "../Scene/SceneEvents.h"

#include "../DebugNew.h"

#ifdef _MSC_VER
#pragma warning(disable:4355)
#endif

namespace Urho3D
{

namespace
{

/// Unused vector of drawables.
static ea::vector<Drawable*> unusedDrawablesVector;

}

static const float DEFAULT_OCTREE_SIZE = 1000.0f;
static const int DEFAULT_OCTREE_LEVELS = 8;

extern const char* SUBSYSTEM_CATEGORY;

void UpdateDrawablesWork(const WorkItem* item, unsigned threadIndex)
{
    URHO3D_PROFILE("UpdateDrawablesWork");
    const FrameInfo& frame = *(reinterpret_cast<FrameInfo*>(item->aux_));
    auto** start = reinterpret_cast<Drawable**>(item->start_);
    auto** end = reinterpret_cast<Drawable**>(item->end_);

    while (start != end)
    {
        Drawable* drawable = *start;
        if (drawable)
            drawable->Update(frame);
        ++start;
    }
}

inline bool CompareRayQueryResults(const RayQueryResult& lhs, const RayQueryResult& rhs)
{
    return lhs.distance_ < rhs.distance_;
}

Octant::Octant(const BoundingBox& box, unsigned level, Octant* parent, Octree* octree, unsigned index) :
    level_(level),
    parent_(parent),
    octree_(octree),
    index_(index)
{
    Initialize(box);
}

Octant::~Octant()
{
    if (octree_)
    {
        Octant* rootOctant = octree_->GetRootOctant();

        // Remove the drawables (if any) from this octant to the root octant
        for (auto i = drawables_.begin(); i != drawables_.end(); ++i)
        {
            (*i)->SetOctant(rootOctant);
            rootOctant->drawables_.push_back(*i);
            octree_->QueueUpdate(*i);
        }
        drawables_.clear();
        numDrawables_ = 0;
    }

    for (unsigned i = 0; i < NUM_OCTANTS; ++i)
        DeleteChild(i);
}

Octant* Octant::GetOrCreateChild(unsigned index)
{
    if (children_[index])
        return children_[index];

    Vector3 newMin = worldBoundingBox_.min_;
    Vector3 newMax = worldBoundingBox_.max_;
    Vector3 oldCenter = worldBoundingBox_.Center();

    if (index & 1u)
        newMin.x_ = oldCenter.x_;
    else
        newMax.x_ = oldCenter.x_;

    if (index & 2u)
        newMin.y_ = oldCenter.y_;
    else
        newMax.y_ = oldCenter.y_;

    if (index & 4u)
        newMin.z_ = oldCenter.z_;
    else
        newMax.z_ = oldCenter.z_;

    children_[index] = new Octant(BoundingBox(newMin, newMax), level_ + 1, this, octree_, index);
    return children_[index];
}

void Octant::DeleteChild(unsigned index)
{
    assert(index < NUM_OCTANTS);
    delete children_[index];
    children_[index] = nullptr;
}

void Octant::InsertDrawable(Drawable* drawable)
{
    const BoundingBox& box = drawable->GetWorldBoundingBox();

    // If root octant, insert all non-occludees here, so that octant occlusion does not hide the drawable.
    // Also if drawable is outside the root octant bounds, insert to root
    bool insertHere;
    if (this == octree_->GetRootOctant())
        insertHere = !drawable->IsOccludee() || cullingBox_.IsInside(box) != INSIDE || CheckDrawableFit(box);
    else
        insertHere = CheckDrawableFit(box);

    if (insertHere)
    {
        Octant* oldOctant = drawable->octant_;
        if (oldOctant != this)
        {
            // Add first, then remove, because drawable count going to zero deletes the octree branch in question
            AddDrawable(drawable);
            if (oldOctant)
                oldOctant->RemoveDrawable(drawable, false);
        }
    }
    else
    {
        Vector3 boxCenter = box.Center();
        unsigned x = boxCenter.x_ < center_.x_ ? 0 : 1;
        unsigned y = boxCenter.y_ < center_.y_ ? 0 : 2;
        unsigned z = boxCenter.z_ < center_.z_ ? 0 : 4;

        GetOrCreateChild(x + y + z)->InsertDrawable(drawable);
    }
}

bool Octant::CheckDrawableFit(const BoundingBox& box) const
{
    Vector3 boxSize = box.Size();

    // If max split level, size always OK, otherwise check that box is at least half size of octant
    if (level_ >= octree_->GetNumLevels() || boxSize.x_ >= halfSize_.x_ || boxSize.y_ >= halfSize_.y_ ||
        boxSize.z_ >= halfSize_.z_)
        return true;
    // Also check if the box can not fit a child octant's culling box, in that case size OK (must insert here)
    else
    {
        if (box.min_.x_ <= worldBoundingBox_.min_.x_ - 0.5f * halfSize_.x_ ||
            box.max_.x_ >= worldBoundingBox_.max_.x_ + 0.5f * halfSize_.x_ ||
            box.min_.y_ <= worldBoundingBox_.min_.y_ - 0.5f * halfSize_.y_ ||
            box.max_.y_ >= worldBoundingBox_.max_.y_ + 0.5f * halfSize_.y_ ||
            box.min_.z_ <= worldBoundingBox_.min_.z_ - 0.5f * halfSize_.z_ ||
            box.max_.z_ >= worldBoundingBox_.max_.z_ + 0.5f * halfSize_.z_)
            return true;
    }

    // Bounding box too small, should create a child octant
    return false;
}

void Octant::SetRootSize(const BoundingBox& box)
{
    // If drawables exist, they are temporarily moved to the root
    for (unsigned i = 0; i < NUM_OCTANTS; ++i)
        DeleteChild(i);

    Initialize(box);
    numDrawables_ = drawables_.size();
}

void Octant::ResetOctree()
{
    octree_ = nullptr;

    // The whole octree is being destroyed, just detach the drawables
    for (Drawable* drawable : drawables_)
    {
        drawable->SetOctant(nullptr);
        drawable->SetDrawableIndex(M_MAX_UNSIGNED);
    }

    for (auto& child : children_)
    {
        if (child)
            child->ResetOctree();
    }
}

void Octant::DrawDebugGeometry(DebugRenderer* debug, bool depthTest)
{
    if (debug && debug->IsInside(worldBoundingBox_))
    {
        debug->AddBoundingBox(worldBoundingBox_, Color(0.25f, 0.25f, 0.25f), depthTest);

        for (auto& child : children_)
        {
            if (child)
                child->DrawDebugGeometry(debug, depthTest);
        }
    }
}

void Octant::Initialize(const BoundingBox& box)
{
    worldBoundingBox_ = box;
    center_ = box.Center();
    halfSize_ = 0.5f * box.Size();
    cullingBox_ = BoundingBox(worldBoundingBox_.min_ - halfSize_, worldBoundingBox_.max_ + halfSize_);
}

void Octant::GetDrawablesInternal(OctreeQuery& query, bool inside) const
{
    if (this != octree_->GetRootOctant())
    {
        Intersection res = query.TestOctant(cullingBox_, inside);
        if (res == INSIDE)
            inside = true;
        else if (res == OUTSIDE)
        {
            // Fully outside, so cull this octant, its children & drawables
            return;
        }
    }

    if (drawables_.size())
    {
        auto** start = const_cast<Drawable**>(&drawables_[0]);
        Drawable** end = start + drawables_.size();
        query.TestDrawables(start, end, inside);
    }

    for (auto child : children_)
    {
        if (child)
            child->GetDrawablesInternal(query, inside);
    }
}

void Octant::GetDrawablesInternal(RayOctreeQuery& query) const
{
    float octantDist = query.ray_.HitDistance(cullingBox_);
    if (octantDist >= query.maxDistance_)
        return;

    if (drawables_.size())
    {
        auto** start = const_cast<Drawable**>(&drawables_[0]);
        Drawable** end = start + drawables_.size();

        while (start != end)
        {
            Drawable* drawable = *start++;

            if ((drawable->GetDrawableFlags() & query.drawableFlags_) && (drawable->GetViewMask() & query.viewMask_))
                drawable->ProcessRayQuery(query, query.result_);
        }
    }

    for (auto child : children_)
    {
        if (child)
            child->GetDrawablesInternal(query);
    }
}

void Octant::GetDrawablesOnlyInternal(RayOctreeQuery& query, ea::vector<Drawable*>& drawables) const
{
    float octantDist = query.ray_.HitDistance(cullingBox_);
    if (octantDist >= query.maxDistance_)
        return;

    if (drawables_.size())
    {
        auto** start = const_cast<Drawable**>(&drawables_[0]);
        Drawable** end = start + drawables_.size();

        while (start != end)
        {
            Drawable* drawable = *start++;

            if ((drawable->GetDrawableFlags() & query.drawableFlags_) && (drawable->GetViewMask() & query.viewMask_))
                drawables.push_back(drawable);
        }
    }

    for (auto child : children_)
    {
        if (child)
            child->GetDrawablesOnlyInternal(query, drawables);
    }
}

ZoneLookupIndex::ZoneLookupIndex(Context* context)
{
    if (auto renderer = context->GetSubsystem<Renderer>())
        defaultZone_ = renderer->GetDefaultZone();
}

void ZoneLookupIndex::AddZone(Zone* zone)
{
    assert(!zones_.contains(zone));
    zones_.push_back(zone);
    zonesDirty_ = true;
}

void ZoneLookupIndex::UpdateZone(Zone* zone)
{
    assert(zones_.contains(zone));
    zonesDirty_ = true;
}

void ZoneLookupIndex::RemoveZone(Zone* zone)
{
    const unsigned index = zones_.index_of(zone);
    assert(index < zones_.size());
    zones_.erase_at(index);
}

void ZoneLookupIndex::Commit()
{
    if (zonesDirty_)
    {
        zonesDirty_ = false;

        // Sort zones by priority from high to low
        const auto greaterPriority = [](Zone* lhs, Zone* rhs) { return lhs->GetPriority() > rhs->GetPriority(); };
        ea::sort(zones_.begin(), zones_.end(), greaterPriority);

        // Update cached data
        zonesData_.resize(zones_.size());
        for (unsigned i = 0; i < zones_.size(); ++i)
        {
            Zone* zone = zones_[i];
            ZoneData& data = zonesData_[i];
            data.zoneMask_ = zone->GetZoneMask();
            data.boundingBox_ = zone->GetBoundingBox();
            data.inverseWorldTransform_ = zone->GetInverseWorldTransform();
        }
    }

    for (Zone* zone : zones_)
        zone->UpdateCachedData();
    if (defaultZone_)
        defaultZone_->UpdateCachedData();
}

CachedDrawableZone ZoneLookupIndex::QueryZone(const Vector3& position, unsigned zoneMask) const
{
    float minDistanceToOtherZone = M_LARGE_VALUE;
    float distanceToBestZone = M_LARGE_VALUE;
    Zone* bestZone = nullptr;

    const unsigned numZones = zones_.size();
    for (unsigned i = 0; i < numZones; ++i)
    {
        const ZoneData& data = zonesData_[i];
        if ((data.zoneMask_ & zoneMask) == 0)
            continue;

        const Vector3 localPosition = data.inverseWorldTransform_ * position;
        const float signedDistance = data.boundingBox_.SignedDistanceToPoint(localPosition);

        if (signedDistance > 0.0f)
        {
            // Zone cannot affect point, keep distance for cache
            minDistanceToOtherZone = ea::min(minDistanceToOtherZone, signedDistance);
        }
        else if (!bestZone)
        {
            // Zone may affect point
            bestZone = zones_[i];
            distanceToBestZone = -signedDistance;
        }
    }

    const float cacheInvalidationDistance = ea::min(minDistanceToOtherZone, distanceToBestZone);
    return { bestZone ? bestZone : defaultZone_, position, cacheInvalidationDistance * cacheInvalidationDistance };
}

Zone* ZoneLookupIndex::GetBackgroundZone() const
{
    return !zones_.empty() && zones_.back()->GetPriority() <= 0 ? zones_.back() : defaultZone_;
}

Octree::Octree(Context* context) :
    Component(context),
    rootOctant_(BoundingBox(-DEFAULT_OCTREE_SIZE, DEFAULT_OCTREE_SIZE), 0, nullptr, this),
    numLevels_(DEFAULT_OCTREE_LEVELS),
    worldBoundingBox_(rootOctant_.GetWorldBoundingBox()),
    zones_(context)
{
    // If the engine is running headless, subscribe to RenderUpdate events for manually updating the octree
    // to allow raycasts and animation update
    if (!GetSubsystem<Graphics>())
        SubscribeToEvent(E_RENDERUPDATE, URHO3D_HANDLER(Octree, HandleRenderUpdate));
}

Octree::~Octree()
{
    // Reset root pointer from all child octants now so that they do not move their drawables to root
    drawableUpdates_.clear();
    rootOctant_.ResetOctree();
}

void Octree::RegisterObject(Context* context)
{
    context->RegisterFactory<Octree>(SUBSYSTEM_CATEGORY);

    Vector3 defaultBoundsMin = -Vector3::ONE * DEFAULT_OCTREE_SIZE;
    Vector3 defaultBoundsMax = Vector3::ONE * DEFAULT_OCTREE_SIZE;

    URHO3D_ATTRIBUTE_EX("Bounding Box Min", Vector3, worldBoundingBox_.min_, UpdateOctreeSize, defaultBoundsMin, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Bounding Box Max", Vector3, worldBoundingBox_.max_, UpdateOctreeSize, defaultBoundsMax, AM_DEFAULT);
    URHO3D_ATTRIBUTE_EX("Number of Levels", int, numLevels_, UpdateOctreeSize, DEFAULT_OCTREE_LEVELS, AM_DEFAULT);
}

void Octree::DrawDebugGeometry(DebugRenderer* debug, bool depthTest)
{
    if (debug)
    {
        URHO3D_PROFILE("OctreeDrawDebug");

        rootOctant_.DrawDebugGeometry(debug, depthTest);
    }
}

void Octree::SetSize(const BoundingBox& box, unsigned numLevels)
{
    URHO3D_PROFILE("ResizeOctree");

    worldBoundingBox_ = box;
    rootOctant_.SetRootSize(box);
    numLevels_ = Max(numLevels, 1U);
}

void Octree::Update(const FrameInfo& frame)
{
    if (!Thread::IsMainThread())
    {
        URHO3D_LOGERROR("Octree::Update() can not be called from worker threads");
        return;
    }

    // Let drawables update themselves before reinsertion. This can be used for animation
    if (!drawableUpdates_.empty())
    {
        URHO3D_PROFILE("UpdateDrawables");

        // Perform updates in worker threads. Notify the scene that a threaded update is going on and components
        // (for example physics objects) should not perform non-threadsafe work when marked dirty
        Scene* scene = GetScene();
        auto* queue = GetSubsystem<WorkQueue>();
        scene->BeginThreadedUpdate();

        int numWorkItems = queue->GetNumThreads() + 1; // Worker threads + main thread
        int drawablesPerItem = Max((int)(drawableUpdates_.size() / numWorkItems), 1);

        auto start = drawableUpdates_.begin();
        // Create a work item for each thread
        for (int i = 0; i < numWorkItems; ++i)
        {
            SharedPtr<WorkItem> item = queue->GetFreeItem();
            item->priority_ = M_MAX_UNSIGNED;
            item->workFunction_ = UpdateDrawablesWork;
            item->aux_ = const_cast<FrameInfo*>(&frame);

            auto end = drawableUpdates_.end();
            if (i < numWorkItems - 1 && end - start > drawablesPerItem)
                end = start + drawablesPerItem;

            item->start_ = &(*start);
            item->end_ = &(*end);
            queue->AddWorkItem(item);

            start = end;
        }

        queue->Complete(M_MAX_UNSIGNED);
        scene->EndThreadedUpdate();
    }

    // If any drawables were inserted during threaded update, update them now from the main thread
    if (!threadedDrawableUpdates_.empty())
    {
        URHO3D_PROFILE("UpdateDrawablesQueuedDuringUpdate");

        for (auto i = threadedDrawableUpdates_.begin(); i !=
            threadedDrawableUpdates_.end(); ++i)
        {
            Drawable* drawable = *i;
            if (drawable)
            {
                drawable->Update(frame);
                drawableUpdates_.push_back(drawable);
            }
        }

        threadedDrawableUpdates_.clear();
    }

    // Notify drawable update being finished. Custom animation (eg. IK) can be done at this point
    Scene* scene = GetScene();
    if (scene)
    {
        using namespace SceneDrawableUpdateFinished;

        VariantMap& eventData = GetEventDataMap();
        eventData[P_SCENE] = scene;
        eventData[P_TIMESTEP] = frame.timeStep_;
        scene->SendEvent(E_SCENEDRAWABLEUPDATEFINISHED, eventData);
    }

    // Reinsert drawables that have been moved or resized, or that have been newly added to the octree and do not sit inside
    // the proper octant yet
    if (!drawableUpdates_.empty())
    {
        URHO3D_PROFILE("ReinsertToOctree");

        for (auto i = drawableUpdates_.begin(); i != drawableUpdates_.end(); ++i)
        {
            Drawable* drawable = *i;
            drawable->updateQueued_ = false;
            Octant* octant = drawable->GetOctant();
            const BoundingBox& box = drawable->GetWorldBoundingBox();

            // Skip if no octant or does not belong to this octree anymore
            if (!octant || octant->GetOctree() != this)
                continue;
            // Skip if still fits the current octant
            if (drawable->IsOccludee() && octant->GetCullingBox().IsInside(box) == INSIDE && octant->CheckDrawableFit(box))
                continue;

            rootOctant_.InsertDrawable(drawable);

#ifdef _DEBUG
            // Verify that the drawable will be culled correctly
            octant = drawable->GetOctant();
            if (octant != GetRootOctant() && octant->GetCullingBox().IsInside(box) != INSIDE)
            {
                URHO3D_LOGERROR("Drawable is not fully inside its octant's culling bounds: drawable box " + box.ToString() +
                         " octant box " + octant->GetCullingBox().ToString());
            }
#endif
        }
    }

    drawableUpdates_.clear();
    zones_.Commit();
}

void Octree::AddManualDrawable(Drawable* drawable)
{
    if (!drawable || drawable->GetOctant())
        return;

    AddDrawable(drawable);
}

void Octree::RemoveManualDrawable(Drawable* drawable)
{
    if (!drawable)
        return;

    Octant* octant = drawable->GetOctant();
    if (octant && octant->GetOctree() == this)
        RemoveDrawable(drawable, octant);
}

void Octree::AddDrawable(Drawable* drawable)
{
    if (drawable->GetDrawableIndex() != M_MAX_UNSIGNED)
    {
        URHO3D_LOGERROR("Cannot add Drawable that is already added to Octree");
        assert(0);
        return;
    }

    // Add drawable to index
    const unsigned index = drawables_.size();
    drawables_.push_back(drawable);
    drawable->SetDrawableIndex(index);

    // Insert drawable to common Octree
    rootOctant_.InsertDrawable(drawable);

    // Insert drawable to zone index
    if (drawable->GetDrawableFlags().Test(DRAWABLE_ZONE))
    {
        const auto zone = dynamic_cast<Zone*>(drawable);
        if (!zone)
        {
            URHO3D_LOGERROR("Only Zone can be flagged as DRAWABLE_ZONE");
            return;
        }
        zones_.AddZone(zone);
        zone->ClearDrawablesZone();
    }
}

void Octree::RemoveDrawable(Drawable* drawable, Octant* octant)
{
    const unsigned index = drawable->GetDrawableIndex();
    if (index >= drawables_.size() || drawables_[index] != drawable)
    {
        URHO3D_LOGERROR("Cannot remove Drawable that doesn't belong to Octree");
        assert(0);
        return;
    }

    // Remove drawable from Octree
    octant->RemoveDrawable(drawable);

    // Remove drawable from Zone index
    if (drawable->GetDrawableFlags().Test(DRAWABLE_ZONE))
    {
        const auto zone = dynamic_cast<Zone*>(drawable);
        assert(zone);
        zones_.RemoveZone(zone);
    }

    // Remove drawable from index
    if (drawables_.size() > 1)
    {
        Drawable* replacement = drawables_.back();
        drawables_[index] = replacement;
        replacement->SetDrawableIndex(index);
    }
    drawables_.pop_back();
    drawable->SetDrawableIndex(M_MAX_UNSIGNED);
    drawable->updateQueued_ = false;
}

void Octree::MarkZoneDirty(Zone* zone)
{
    zones_.UpdateZone(zone);
}

void Octree::GetDrawables(OctreeQuery& query) const
{
    query.result_.clear();
    rootOctant_.GetDrawablesInternal(query, false);
}

void Octree::Raycast(RayOctreeQuery& query) const
{
    URHO3D_PROFILE("Raycast");

    query.result_.clear();
    rootOctant_.GetDrawablesInternal(query);
    ea::quick_sort(query.result_.begin(), query.result_.end(), CompareRayQueryResults);
}

void Octree::RaycastSingle(RayOctreeQuery& query) const
{
    URHO3D_PROFILE("Raycast");

    query.result_.clear();
    rayQueryDrawables_.clear();
    rootOctant_.GetDrawablesOnlyInternal(query, rayQueryDrawables_);

    // Sort by increasing hit distance to AABB
    for (auto i = rayQueryDrawables_.begin(); i != rayQueryDrawables_.end(); ++i)
    {
        Drawable* drawable = *i;
        drawable->SetSortValue(query.ray_.HitDistance(drawable->GetWorldBoundingBox()));
    }

    ea::quick_sort(rayQueryDrawables_.begin(), rayQueryDrawables_.end(), CompareDrawables);

    // Then do the actual test according to the query, and early-out as possible
    float closestHit = M_INFINITY;
    for (auto i = rayQueryDrawables_.begin(); i != rayQueryDrawables_.end(); ++i)
    {
        Drawable* drawable = *i;
        if (drawable->GetSortValue() < Min(closestHit, query.maxDistance_))
        {
            unsigned oldSize = query.result_.size();
            drawable->ProcessRayQuery(query, query.result_);
            if (query.result_.size() > oldSize)
                closestHit = Min(closestHit, query.result_.back().distance_);
        }
        else
            break;
    }

    if (query.result_.size() > 1)
    {
        ea::quick_sort(query.result_.begin(), query.result_.end(), CompareRayQueryResults);
        query.result_.resize(1);
    }
}

CachedDrawableZone Octree::QueryZone(Drawable* drawable) const
{
    return zones_.QueryZone(drawable->GetWorldBoundingBox().Center(), drawable->GetZoneMask());
}

CachedDrawableZone Octree::QueryZone(const Vector3& drawablePosition, unsigned zoneMask) const
{
    return zones_.QueryZone(drawablePosition, zoneMask);
}

Zone* Octree::GetBackgroundZone() const
{
    return zones_.GetBackgroundZone();
}

void Octree::QueueUpdate(Drawable* drawable)
{
    Scene* scene = GetScene();
    if (scene && scene->IsThreadedUpdate())
    {
        MutexLock lock(octreeMutex_);
        threadedDrawableUpdates_.push_back(drawable);
    }
    else
        drawableUpdates_.push_back(drawable);

    drawable->updateQueued_ = true;
}

void Octree::CancelUpdate(Drawable* drawable)
{
    // This doesn't have to take into account scene being in threaded update, because it is called only
    // when removing a drawable from octree, which should only ever happen from the main thread.
    drawableUpdates_.erase_first(drawable);
    drawable->updateQueued_ = false;
}

void Octree::DrawDebugGeometry(bool depthTest)
{
    auto* debug = GetComponent<DebugRenderer>();
    DrawDebugGeometry(debug, depthTest);
}

void Octree::HandleRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    // When running in headless mode, update the Octree manually during the RenderUpdate event
    Scene* scene = GetScene();
    if (!scene || !scene->IsUpdateEnabled())
        return;

    using namespace RenderUpdate;

    FrameInfo frame;
    frame.frameNumber_ = GetSubsystem<Time>()->GetFrameNumber();
    frame.timeStep_ = eventData[P_TIMESTEP].GetFloat();
    frame.camera_ = nullptr;

    Update(frame);
}

}
