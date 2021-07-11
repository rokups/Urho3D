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

/// \file

#pragma once

#include "../Graphics/GraphicsDefs.h"
#include "../Graphics/PipelineStateTracker.h"
#include "../Math/BoundingBox.h"
#include "../Scene/Component.h"

namespace Urho3D
{

enum DrawableFlag : unsigned char
{
    DRAWABLE_UNDEFINED = 0x0,
    DRAWABLE_GEOMETRY = 0x1,
    DRAWABLE_LIGHT = 0x2,
    DRAWABLE_ZONE = 0x4,
    DRAWABLE_GEOMETRY2D = 0x8,
    DRAWABLE_ANY = 0xff,
};
URHO3D_FLAGSET(DrawableFlag, DrawableFlags);

static const unsigned DEFAULT_VIEWMASK = M_MAX_UNSIGNED;
static const unsigned DEFAULT_LIGHTMASK = M_MAX_UNSIGNED;
static const unsigned DEFAULT_SHADOWMASK = M_MAX_UNSIGNED;
static const unsigned DEFAULT_ZONEMASK = M_MAX_UNSIGNED;
static const unsigned PORTABLE_LIGHTMASK = 0xf;
static const int MAX_VERTEX_LIGHTS = 4;
static const float ANIMATION_LOD_BASESCALE = 2500.0f;

class Camera;
class File;
class Geometry;
class Light;
class Material;
class OcclusionBuffer;
class Octree;
class Octant;
class RayOctreeQuery;
class RenderSurface;
class Viewport;
class Zone;
struct RayQueryResult;
struct WorkItem;

/// Geometry update type.
enum UpdateGeometryType
{
    UPDATE_NONE = 0,
    UPDATE_MAIN_THREAD,
    UPDATE_WORKER_THREAD
};

/// Global illumination mode.
enum class GlobalIlluminationType
{
    None,
    UseLightMap,
    BlendLightProbes,
};

/// Rendering frame update parameters.
struct FrameInfo
{
    /// Frame number.
    unsigned frameNumber_{};
    /// Time elapsed since last frame.
    float timeStep_{};
    /// Viewport size.
    IntVector2 viewSize_;
    /// Viewport rectangle.
    IntRect viewRect_;

    /// Destination viewport.
    Viewport* viewport_{};
    /// Destination render surface.
    RenderSurface* renderTarget_{};

    /// Scene being rendered.
    Scene* scene_{};
    /// Camera being used for drawable culling.
    Camera* camera_{};
    /// Octree being used for queries.
    Octree* octree_{};
};

/// Cached info about current zone.
struct CachedDrawableZone
{
    /// Pointer to Zone.
    Zone* zone_{};
    /// Node position at the moment of last caching.
    Vector3 cachePosition_;
    /// Cache invalidation distance (squared).
    float cacheInvalidationDistanceSquared_{ -1.0f };
};

/// Source data for a 3D geometry draw call.
struct URHO3D_API SourceBatch
{
    /// Construct with defaults.
    SourceBatch();
    /// Copy-construct.
    SourceBatch(const SourceBatch& batch);
    /// Destruct.
    ~SourceBatch();

    /// Assignment operator.
    SourceBatch& operator =(const SourceBatch& rhs);

    /// Distance from camera.
    float distance_{};
    /// Geometry.
    Geometry* geometry_{};
    /// Material.
    SharedPtr<Material> material_;
    /// World transform(s). For a skinned model, these are the bone transforms.
    const Matrix3x4* worldTransform_{&Matrix3x4::IDENTITY};
    /// Number of world transforms.
    unsigned numWorldTransforms_{1};
    /// Per-instance data. If not null, must contain enough data to fill instancing buffer.
    void* instancingData_{};
    /// %Geometry type.
    GeometryType geometryType_{GEOM_STATIC};
    /// Lightmap UV scale and offset.
    Vector4* lightmapScaleOffset_{};
    /// Lightmap texture index.
    unsigned lightmapIndex_{};

    /// Equality comparison operator.
    bool operator==(const SourceBatch& other) const
    {
        if (this == &other)
            return true;
        return distance_ == other.distance_ && geometry_ == other.geometry_ && material_ == other.material_ &&
            worldTransform_ == other.worldTransform_ && numWorldTransforms_ == other.numWorldTransforms_ &&
            instancingData_ == other.instancingData_ && geometryType_ == other.geometryType_;
    }

    /// Inequality comparison operator.
    bool operator!=(const SourceBatch& other) const
    {
        return !(*this == other);
    }
};

/// Base class for visible components.
class URHO3D_API Drawable : public Component, public PipelineStateTracker
{
    URHO3D_OBJECT(Drawable, Component);

    friend class Octant;
    friend class Octree;
    friend void UpdateDrawablesWork(const WorkItem* item, unsigned threadIndex);

public:
    /// Construct.
    Drawable(Context* context, DrawableFlags drawableFlags=DRAWABLE_UNDEFINED);
    /// Destruct.
    ~Drawable() override;
    /// Register object attributes. Drawable must be registered first.
    /// @nobind
    static void RegisterObject(Context* context);

    /// Handle enabled/disabled state change.
    void OnSetEnabled() override;
    /// Process octree raycast. May be called from a worker thread.
    virtual void ProcessRayQuery(const RayOctreeQuery& query, ea::vector<RayQueryResult>& results);
    /// Update before octree reinsertion. Is called from a worker thread.
    virtual void Update(const FrameInfo& frame) { }
    /// Calculate distance and prepare batches for rendering. May be called from worker thread(s), possibly re-entrantly.
    virtual void UpdateBatches(const FrameInfo& frame);
    /// Prepare geometry for rendering.
    virtual void UpdateGeometry(const FrameInfo& frame) { }

    /// Return whether a geometry update is necessary, and if it can happen in a worker thread.
    virtual UpdateGeometryType GetUpdateGeometryType() { return UPDATE_NONE; }

    /// Return the geometry for a specific LOD level.
    virtual Geometry* GetLodGeometry(unsigned batchIndex, unsigned level);

    /// Return number of occlusion geometry triangles.
    virtual unsigned GetNumOccluderTriangles() { return 0; }

    /// Draw to occlusion buffer. Return true if did not run out of triangles.
    virtual bool DrawOcclusion(OcclusionBuffer* buffer);
    /// Visualize the component as debug geometry.
    void DrawDebugGeometry(DebugRenderer* debug, bool depthTest) override;

    /// Set draw distance.
    /// @property
    void SetDrawDistance(float distance);
    /// Set shadow draw distance.
    /// @property
    void SetShadowDistance(float distance);
    /// Set LOD bias.
    /// @property
    void SetLodBias(float bias);
    /// Set view mask. Is and'ed with camera's view mask to see if the object should be rendered.
    /// @property
    void SetViewMask(unsigned mask);
    /// Set light mask. Is and'ed with light's and zone's light mask to see if the object should be lit.
    /// @property
    void SetLightMask(unsigned mask);
    /// Set shadow mask. Is and'ed with light's light mask and zone's shadow mask to see if the object should be rendered to a shadow map.
    /// @property
    void SetShadowMask(unsigned mask);
    /// Set zone mask. Is and'ed with zone's zone mask to see if the object should belong to the zone.
    /// @property
    void SetZoneMask(unsigned mask);
    /// Set maximum number of per-pixel lights. Default 0 is unlimited.
    /// @property
    void SetMaxLights(unsigned num);
    /// Set shadowcaster flag.
    /// @property
    void SetCastShadows(bool enable);
    /// Set occlusion flag.
    /// @property
    void SetOccluder(bool enable);
    /// Set occludee flag.
    /// @property
    void SetOccludee(bool enable);
    /// Set GI type.
    void SetGlobalIlluminationType(GlobalIlluminationType type);
    /// Mark for update and octree reinsertion. Update is automatically queued when the drawable's scene node moves or changes scale.
    void MarkForUpdate();

    /// Return local space bounding box. May not be applicable or properly updated on all drawables.
    /// @property
    const BoundingBox& GetBoundingBox() const { return boundingBox_; }

    /// Return world-space bounding box.
    /// @property
    const BoundingBox& GetWorldBoundingBox();

    /// Return drawable flags.
    DrawableFlags GetDrawableFlags() const { return drawableFlags_; }

    /// Return draw distance.
    /// @property
    float GetDrawDistance() const { return drawDistance_; }

    /// Return shadow draw distance.
    /// @property
    float GetShadowDistance() const { return shadowDistance_; }

    /// Return LOD bias.
    /// @property
    float GetLodBias() const { return lodBias_; }

    /// Return view mask.
    /// @property
    unsigned GetViewMask() const { return viewMask_; }

    /// Return light mask.
    /// @property
    unsigned GetLightMask() const { return lightMask_; }

    /// Return shadow mask.
    /// @property
    unsigned GetShadowMask() const { return shadowMask_; }

    /// Return zone mask.
    /// @property
    unsigned GetZoneMask() const { return zoneMask_; }

    /// Return maximum number of per-pixel lights.
    /// @property
    unsigned GetMaxLights() const { return maxLights_; }

    /// Return shadowcaster flag.
    /// @property
    bool GetCastShadows() const { return castShadows_; }

    /// Return occluder flag.
    /// @property
    bool IsOccluder() const { return occluder_; }

    /// Return occludee flag.
    /// @property
    bool IsOccludee() const { return occludee_; }

    /// Return global illumination type.
    GlobalIlluminationType GetGlobalIlluminationType() const { return giType_; }

    /// Return whether is in view this frame from any viewport camera. Excludes shadow map cameras.
    /// @property
    bool IsInView() const;
    /// Return whether is in view of a specific camera this frame. Pass in a null camera to allow any camera, including shadow map cameras.
    bool IsInView(Camera* camera) const;

    /// Return draw call source data.
    const ea::vector<SourceBatch>& GetBatches() const { return batches_; }

    /// Set new zone. Zone assignment may optionally be temporary, meaning it needs to be re-evaluated on the next frame.
    void SetZone(Zone* zone, bool temporary = false);
    /// Set sorting value.
    void SetSortValue(float value);

    /// Set view-space depth bounds.
    void SetMinMaxZ(float minZ, float maxZ)
    {
        minZ_ = minZ;
        maxZ_ = maxZ;
    }

    /// Mark in view. Also clear the light list.
    void MarkInView(const FrameInfo& frame);
    /// Mark in view without specifying a camera. Used for shadow casters.
    void MarkInView(unsigned frameNumber);
    /// Sort and limit per-pixel lights to maximum allowed. Convert extra lights into vertex lights.
    void LimitLights();
    /// Sort and limit per-vertex lights to maximum allowed.
    void LimitVertexLights(bool removeConvertedLights);

    /// Set base pass flag for a batch.
    void SetBasePass(unsigned batchIndex) { basePassFlags_ |= (1u << batchIndex); }

    /// Return octree octant.
    Octant* GetOctant() const { return octant_; }

    /// Return index in octree.
    unsigned GetDrawableIndex() const { return drawableIndex_; }

    /// Return whether the drawable is added to Octree.
    bool IsInOctree() const { return drawableIndex_ != M_MAX_UNSIGNED; }

    /// Return current zone.
    /// @property
    Zone* GetZone() const { return cachedZone_.zone_; }

    /// Return whether current zone is inconclusive or dirty due to the drawable moving.
    bool IsZoneDirty() const { return zoneDirty_; }

    /// Return distance from camera.
    float GetDistance() const { return distance_; }

    /// Return LOD scaled distance from camera.
    float GetLodDistance() const { return lodDistance_; }

    /// Return sorting value.
    float GetSortValue() const { return sortValue_; }

    /// Return whether is in view on the current frame. Called by View.
    bool IsInView(const FrameInfo& frame, bool anyCamera = false) const;

    /// Return whether has a base pass.
    bool HasBasePass(unsigned batchIndex) const { return (basePassFlags_ & (1u << batchIndex)) != 0; }

    /// Return per-pixel lights.
    const ea::vector<Light*>& GetLights() const { return lights_; }

    /// Return per-vertex lights.
    const ea::vector<Light*>& GetVertexLights() const { return vertexLights_; }

    /// Return the first added per-pixel light.
    Light* GetFirstLight() const { return firstLight_; }

    /// Return the minimum view-space depth.
    float GetMinZ() const { return minZ_; }

    /// Return the maximum view-space depth.
    float GetMaxZ() const { return maxZ_; }

    /// Return mutable light probe tetrahedron hint.
    unsigned& GetMutableLightProbeTetrahedronHint() { return lightProbeTetrahedronHint_; }

    /// Return mutable cached zone data.
    CachedDrawableZone& GetMutableCachedZone() { return cachedZone_; }

    /// Return combined light masks of Drawable and its currently cached Zone.
    unsigned GetLightMaskInZone() const;

    /// Return combined shadow masks of Drawable and its currently cached Zone.
    unsigned GetShadowMaskInZone() const;

    /// Add a per-pixel light affecting the object this frame.
    void AddLight(Light* light)
    {
        if (!firstLight_)
            firstLight_ = light;

        // Need to store into the light list only if the per-pixel lights are being limited
        // Otherwise recording the first light is enough
        if (maxLights_)
            lights_.push_back(light);
    }

    /// Add a per-vertex light affecting the object this frame.
    void AddVertexLight(Light* light)
    {
        vertexLights_.push_back(light);
    }

protected:
    /// Recalculate hash. Shall be save to call from multiple threads as long as the object is not changing.
    unsigned RecalculatePipelineStateHash() const override;

    /// Handle node being assigned.
    void OnNodeSet(Node* node) override;
    /// Handle scene being assigned.
    void OnSceneSet(Scene* scene) override;
    /// Handle node transform being dirtied.
    void OnMarkedDirty(Node* node) override;
    /// Recalculate the world-space bounding box.
    virtual void OnWorldBoundingBoxUpdate() = 0;

    /// Handle removal from octree.
    virtual void OnRemoveFromOctree() { }

    /// Add to octree.
    void AddToOctree();
    /// Remove from octree.
    void RemoveFromOctree();

    /// Move into another octree octant.
    void SetOctant(Octant* octant) { octant_ = octant; }
    /// Update drawable index.
    void SetDrawableIndex(unsigned drawableIndex) { drawableIndex_ = drawableIndex; };

    /// World-space bounding box.
    BoundingBox worldBoundingBox_;
    /// Local-space bounding box.
    BoundingBox boundingBox_;
    /// Draw call source data.
    ea::vector<SourceBatch> batches_;
    /// Drawable flags.
    DrawableFlags drawableFlags_;
    /// Global illumination type.
    GlobalIlluminationType giType_{};
    /// Bounding box dirty flag.
    bool worldBoundingBoxDirty_;
    /// Shadowcaster flag.
    bool castShadows_;
    /// Occluder flag.
    bool occluder_;
    /// Occludee flag.
    bool occludee_;
    /// Octree update queued flag.
    bool updateQueued_;
    /// Zone inconclusive or dirtied flag.
    bool zoneDirty_;
    /// Octree octant.
    Octant* octant_;
    /// Index of Drawable in Scene. May be updated.
    unsigned drawableIndex_{ M_MAX_UNSIGNED };
    /// Current zone.
    CachedDrawableZone cachedZone_;
    /// View mask.
    unsigned viewMask_;
    /// Light mask.
    unsigned lightMask_;
    /// Shadow mask.
    unsigned shadowMask_;
    /// Zone mask.
    unsigned zoneMask_;
    /// Last visible frame number.
    unsigned viewFrameNumber_;
    /// Current distance to camera.
    float distance_;
    /// LOD scaled distance.
    float lodDistance_;
    /// Draw distance.
    float drawDistance_;
    /// Shadow distance.
    float shadowDistance_;
    /// Current sort value.
    float sortValue_;
    /// Current minimum view space depth.
    float minZ_;
    /// Current maximum view space depth.
    float maxZ_;
    /// LOD bias.
    float lodBias_;
    /// Light probe tetrahedron hint.
    unsigned lightProbeTetrahedronHint_{ M_MAX_UNSIGNED };
    /// Base pass flags, bit per batch.
    unsigned basePassFlags_;
    /// Maximum per-pixel lights.
    unsigned maxLights_;
    /// List of cameras from which is seen on the current frame.
    ea::vector<Camera*> viewCameras_;
    /// First per-pixel light added this frame.
    Light* firstLight_;
    /// Per-pixel lights affecting this drawable.
    ea::vector<Light*> lights_;
    /// Per-vertex lights affecting this drawable.
    ea::vector<Light*> vertexLights_;
};

inline bool CompareDrawables(const Drawable* lhs, const Drawable* rhs)
{
    return lhs->GetSortValue() < rhs->GetSortValue();
}

URHO3D_API bool WriteDrawablesToOBJ(const ea::vector<Drawable*>& drawables, File* outputFile, bool asZUp, bool asRightHanded, bool writeLightmapUV = false);

}
