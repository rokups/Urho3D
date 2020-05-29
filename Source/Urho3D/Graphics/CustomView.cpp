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

#include "../Core/Context.h"
#include "../Core/IteratorRange.h"
#include "../Graphics/Camera.h"
#include "../Graphics/CustomView.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Viewport.h"
#include "../Scene/Scene.h"

#include <EASTL/fixed_vector.h>
#include <EASTL/vector_map.h>
#include <EASTL/sort.h>

#include "../Graphics/Light.h"
#include "../Graphics/Octree.h"
#include "../Graphics/Geometry.h"
#include "../Graphics/Batch.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/Zone.h"
#include "../Graphics/PipelineStateCache.h"
#include "../Core/WorkQueue.h"

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

/// Helper class to evaluate min and max Z of the drawable.
struct DrawableZRangeEvaluator
{
    explicit DrawableZRangeEvaluator(Camera* camera)
        : viewMatrix_(camera->GetView())
        , viewZ_(viewMatrix_.m20_, viewMatrix_.m21_, viewMatrix_.m22_)
        , absViewZ_(viewZ_.Abs())
    {
    }

    DrawableZRange Evaluate(Drawable* drawable) const
    {
        const BoundingBox& boundingBox = drawable->GetWorldBoundingBox();
        const Vector3 center = boundingBox.Center();
        const Vector3 edge = boundingBox.Size() * 0.5f;

        // Ignore "infinite" objects like skybox
        if (edge.LengthSquared() >= M_LARGE_VALUE * M_LARGE_VALUE)
            return {};

        const float viewCenterZ = viewZ_.DotProduct(center) + viewMatrix_.m23_;
        const float viewEdgeZ = absViewZ_.DotProduct(edge);
        const float minZ = viewCenterZ - viewEdgeZ;
        const float maxZ = viewCenterZ + viewEdgeZ;

        return { minZ, maxZ };
    }

    Matrix3x4 viewMatrix_;
    Vector3 viewZ_;
    Vector3 absViewZ_;
};

/// Light importance.
enum LightImportance
{
    LI_AUTO,
    LI_IMPORTANT,
    LI_NOT_IMPORTANT
};

/// Return light importance.
// TODO: Move it to Light
LightImportance GetLightImportance(Light* light)
{
    if (light->IsNegative())
        return LI_IMPORTANT;
    else if (light->GetPerVertex())
        return LI_NOT_IMPORTANT;
    else
        return LI_AUTO;
}

/// Return distance between light and geometry.
float GetLightGeometryDistance(Light* light, Drawable* geometry)
{
    if (light->GetLightType() == LIGHT_DIRECTIONAL)
        return 0.0f;

    const BoundingBox boundingBox = geometry->GetWorldBoundingBox();
    const Vector3 lightPosition = light->GetNode()->GetWorldPosition();
    const Vector3 minDelta = boundingBox.min_ - lightPosition;
    const Vector3 maxDelta = lightPosition - boundingBox.max_;
    return VectorMax(Vector3::ZERO, VectorMax(minDelta, maxDelta)).Length();
}

/// Return light penalty.
float GetLightPenalty(Light* light, Drawable* geometry)
{
    const float distance = GetLightGeometryDistance(light, geometry);
    const float intensity = 1.0f / light->GetIntensityDivisor();
    return distance * intensity;
}

/// Light accumulator context.
struct DrawableLightAccumulatorContext
{
    /// Max number of pixel lights.
    unsigned maxPixelLights_{ 1 };
};

/// Light accumulator.
/// MaxPixelLights: Max number of per-pixel lights supported. Important lights ignore this limit.
template <unsigned MaxPixelLights>
struct DrawableLightAccumulator
{
    /// Max number of per-vertex lights supported.
    static const unsigned MaxVertexLights = 4;
    /// Max number of lights that don't require allocations.
    static const unsigned NumElements = ea::max(MaxPixelLights + 1, 4u) + MaxVertexLights;
    /// Container for lights.
    using Container = ea::vector_map<float, Light*, ea::less<float>, ea::allocator,
        ea::fixed_vector<ea::pair<float, Light*>, NumElements>>;

    /// Reset accumulator.
    void Reset()
    {
        lights_.clear();
        numImportantLights_ = 0;
    }

    /// Accumulate light.
    void AccumulateLight(DrawableLightAccumulatorContext& ctx, float penalty, Light* light)
    {
        // Count important lights
        if (GetLightImportance(light) == LI_IMPORTANT)
        {
            penalty = -1.0f;
            ++numImportantLights_;
        }

        // Add new light
        lights_.emplace(penalty, light);

        // If too many lights, drop the least important one
        const unsigned maxLights = MaxVertexLights + ea::max(ctx.maxPixelLights_, numImportantLights_);
        if (lights_.size() > maxLights)
        {
            // TODO: Update SH
            lights_.pop_back();
        }
    }

    /// Container of per-pixel and per-pixel lights.
    Container lights_;
    /// Accumulated SH lights.
    SphericalHarmonicsDot9 sh_;
    /// Number of important lights.
    unsigned numImportantLights_{};
};

/// Process primary drawable.
void ProcessPrimaryDrawable(Drawable* drawable,
    const DrawableZRangeEvaluator& zRangeEvaluator,
    DrawableViewportCache& cache, unsigned threadIndex)
{
    const unsigned drawableIndex = drawable->GetDrawableIndex();
    cache.transient_.traits_[drawableIndex] |= TransientDrawableDataIndex::DrawableUpdated;

    // Skip if too far
    const float maxDistance = drawable->GetDrawDistance();
    if (maxDistance > 0.0f)
    {
        if (drawable->GetDistance() > maxDistance)
            return;
    }

    // For geometries, find zone, clear lights and calculate view space Z range
    if (drawable->GetDrawableFlags() & DRAWABLE_GEOMETRY)
    {
        const DrawableZRange zRange = zRangeEvaluator.Evaluate(drawable);

        // Do not add "infinite" objects like skybox to prevent shadow map focusing behaving erroneously
        if (!zRange.IsValid())
            cache.transient_.zRange_[drawableIndex] = { M_LARGE_VALUE, M_LARGE_VALUE };
        else
        {
            cache.transient_.zRange_[drawableIndex] = zRange;
            cache.sceneZRange_.Accumulate(threadIndex, zRange);
        }

        cache.visibleGeometries_.Insert(threadIndex, drawable);
        cache.transient_.traits_[drawableIndex] |= TransientDrawableDataIndex::DrawableVisibleGeometry;
    }
    else if (drawable->GetDrawableFlags() & DRAWABLE_LIGHT)
    {
        auto light = static_cast<Light*>(drawable);
        const Color lightColor = light->GetEffectiveColor();

        // Skip lights with zero brightness or black color, skip baked lights too
        if (!lightColor.Equals(Color::BLACK) && light->GetLightMaskEffective() != 0)
            cache.visibleLights_.Insert(threadIndex, light);
    }
}

/// Frustum Query for point light.
struct PointLightLitGeometriesQuery : public SphereOctreeQuery
{
    /// Return light sphere for the query.
    static Sphere GetLightSphere(Light* light)
    {
        return Sphere(light->GetNode()->GetWorldPosition(), light->GetRange());
    }

    /// Construct.
    PointLightLitGeometriesQuery(ea::vector<Drawable*>& result,
        const TransientDrawableDataIndex& transientData, Light* light)
        : SphereOctreeQuery(result, GetLightSphere(light), DRAWABLE_GEOMETRY)
        , transientData_(&transientData)
        , lightMask_(light->GetLightMaskEffective())
    {
    }

    void TestDrawables(Drawable** start, Drawable** end, bool inside) override
    {
        for (Drawable* drawable : MakeIteratorRange(start, end))
        {
            const unsigned drawableIndex = drawable->GetDrawableIndex();
            const unsigned traits = transientData_->traits_[drawableIndex];
            if (traits & TransientDrawableDataIndex::DrawableVisibleGeometry)
            {
                if (drawable->GetLightMask() & lightMask_)
                {
                    if (inside || sphere_.IsInsideFast(drawable->GetWorldBoundingBox()))
                        result_.push_back(drawable);
                }
            }
        }
    }

    /// Visiblity cache.
    const TransientDrawableDataIndex* transientData_{};
    /// Light mask to check.
    unsigned lightMask_{};
};

/// Frustum Query for spot light.
struct SpotLightLitGeometriesQuery : public FrustumOctreeQuery
{
    /// Construct.
    SpotLightLitGeometriesQuery(ea::vector<Drawable*>& result,
        const TransientDrawableDataIndex& transientData, Light* light)
        : FrustumOctreeQuery(result, light->GetFrustum(), DRAWABLE_GEOMETRY)
        , transientData_(&transientData)
        , lightMask_(light->GetLightMaskEffective())
    {
    }

    void TestDrawables(Drawable** start, Drawable** end, bool inside) override
    {
        for (Drawable* drawable : MakeIteratorRange(start, end))
        {
            const unsigned drawableIndex = drawable->GetDrawableIndex();
            const unsigned traits = transientData_->traits_[drawableIndex];
            if (traits & TransientDrawableDataIndex::DrawableVisibleGeometry)
            {
                if (drawable->GetLightMask() & lightMask_)
                {
                    if (inside || frustum_.IsInsideFast(drawable->GetWorldBoundingBox()))
                        result_.push_back(drawable);
                }
            }
        }
    }

    /// Visiblity cache.
    const TransientDrawableDataIndex* transientData_{};
    /// Light mask to check.
    unsigned lightMask_{};
};

/// Collection of shader parameters.
class ShaderParameterCollection
{
public:
    /// Return next parameter offset.
    unsigned GetNextParameterOffset() const
    {
        return names_.size();
    }

    /// Add new variant parameter.
    void AddParameter(StringHash name, const Variant& value)
    {
        switch (value.GetType())
        {
        case VAR_BOOL:
            AddParameter(name, value.GetBool() ? 1 : 0);
            break;

        case VAR_INT:
            AddParameter(name, value.GetInt());
            break;

        case VAR_FLOAT:
        case VAR_DOUBLE:
            AddParameter(name, value.GetFloat());
            break;

        case VAR_VECTOR2:
            AddParameter(name, value.GetVector2());
            break;

        case VAR_VECTOR3:
            AddParameter(name, value.GetVector3());
            break;

        case VAR_VECTOR4:
            AddParameter(name, value.GetVector4());
            break;

        case VAR_COLOR:
            AddParameter(name, value.GetColor());
            break;

        case VAR_MATRIX3:
            AddParameter(name, value.GetMatrix3());
            break;

        case VAR_MATRIX3X4:
            AddParameter(name, value.GetMatrix3x4());
            break;

        case VAR_MATRIX4:
            AddParameter(name, value.GetMatrix4());
            break;

        default:
            // Unsupported parameter type, do nothing
            break;
        }
    }

    /// Add new int parameter.
    void AddParameter(StringHash name, int value)
    {
        const int data[4]{ value, 0, 0, 0 };
        AllocateParameter(name, VAR_INTRECT, 1, data, 4);
    }

    /// Add new float parameter.
    void AddParameter(StringHash name, float value)
    {
        const Vector4 data{ value, 0.0f, 0.0f, 0.0f };
        AllocateParameter(name, VAR_VECTOR4, 1, data.Data(), 4);
    }

    /// Add new Vector2 parameter.
    void AddParameter(StringHash name, const Vector2& value)
    {
        const Vector4 data{ value.x_, value.y_, 0.0f, 0.0f };
        AllocateParameter(name, VAR_VECTOR4, 1, data.Data(), 4);
    }

    /// Add new Vector3 parameter.
    void AddParameter(StringHash name, const Vector3& value)
    {
        const Vector4 data{ value, 0.0f };
        AllocateParameter(name, VAR_VECTOR4, 1, data.Data(), 4);
    }

    /// Add new Vector4 parameter.
    void AddParameter(StringHash name, const Vector4& value)
    {
        AllocateParameter(name, VAR_VECTOR4, 1, value.Data(), 4);
    }

    /// Add new Color parameter.
    void AddParameter(StringHash name, const Color& value)
    {
        AllocateParameter(name, VAR_VECTOR4, 1, value.Data(), 4);
    }

    /// Add new Matrix3 parameter.
    void AddParameter(StringHash name, const Matrix3& value)
    {
        const Matrix3x4 data{ value };
        AllocateParameter(name, VAR_MATRIX3X4, 1, data.Data(), 12);
    }

    /// Add new Matrix3x4 parameter.
    void AddParameter(StringHash name, const Matrix3x4& value)
    {
        AllocateParameter(name, VAR_MATRIX3X4, 1, value.Data(), 12);
    }

    /// Add new Matrix4 parameter.
    void AddParameter(StringHash name, const Matrix4& value)
    {
        AllocateParameter(name, VAR_MATRIX4, 1, value.Data(), 16);
    }

    /// Iterate.
    template <class T>
    void ForEach(const T& callback) const
    {
        const unsigned numParameters = names_.size();
        const unsigned char* dataPointer = data_.data();
        for (unsigned i = 0; i < numParameters; ++i)
        {
            const void* rawData = &dataPointer[dataOffsets_[i]];
            const StringHash name = names_[i];
            const VariantType type = dataTypes_[i];
            const unsigned arraySize = dataSizes_[i];
            switch (type)
            {
            case VAR_INTRECT:
            {
                const auto data = reinterpret_cast<const int*>(rawData);
                callback(name, data, arraySize);
                break;
            }
            case VAR_VECTOR4:
            {
                const auto data = reinterpret_cast<const Vector4*>(rawData);
                callback(name, data, arraySize);
                break;
            }
            case VAR_MATRIX3X4:
            {
                const auto data = reinterpret_cast<const Matrix3x4*>(rawData);
                callback(name, data, arraySize);
                break;
            }
            case VAR_MATRIX4:
            {
                const auto data = reinterpret_cast<const Matrix4*>(rawData);
                callback(name, data, arraySize);
                break;
            }
            default:
                break;
            }
        }
    }

private:
    /// Add new parameter.
    template <class T>
    void AllocateParameter(StringHash name, VariantType type, unsigned arraySize, const T* srcData, unsigned count)
    {
        static const unsigned alignment = 4 * sizeof(float);
        const unsigned alignedSize = (count * sizeof(T) + alignment - 1) / alignment * alignment;

        const unsigned offset = data_.size();
        names_.push_back(name);
        dataOffsets_.push_back(offset);
        dataSizes_.push_back(arraySize);
        dataTypes_.push_back(type);
        data_.resize(offset + alignedSize);

        T* destData = reinterpret_cast<T*>(&data_[offset]);
        // TODO: Use memcpy to make it a bit faster in Debug builds?
        //memcpy(destData, srcData, count * sizeof(T));
        ea::copy(srcData, srcData + count, destData);
    }

    /// Parameter names.
    ea::vector<StringHash> names_;
    /// Parameter offsets in data buffer.
    ea::vector<unsigned> dataOffsets_;
    /// Parameter array sizes in data buffer.
    ea::vector<unsigned> dataSizes_;
    /// Parameter types in data buffer.
    ea::vector<VariantType> dataTypes_;
    /// Data buffer.
    ByteVector data_;
};

struct SharedParameterSetter
{
    Graphics* graphics_;
    void operator()(const StringHash& name, const int* data, unsigned arraySize) const
    {
        graphics_->SetShaderParameter(name, *data);
    }
    void operator()(const StringHash& name, const Vector4* data, unsigned arraySize) const
    {
        if (arraySize != 1)
            graphics_->SetShaderParameter(name, data->Data(), 4 * arraySize);
        else
            graphics_->SetShaderParameter(name, *data);
    }
    void operator()(const StringHash& name, const Matrix3x4* data, unsigned arraySize) const
    {
        if (arraySize != 1)
            graphics_->SetShaderParameter(name, data->Data(), 12 * arraySize);
        else
            graphics_->SetShaderParameter(name, *data);
    }
    void operator()(const StringHash& name, const Matrix4* data, unsigned arraySize) const
    {
        if (arraySize != 1)
            graphics_->SetShaderParameter(name, data->Data(), 16 * arraySize);
        else
            graphics_->SetShaderParameter(name, *data);
    }
};

/// Collection of shader resources.
using ShaderResourceCollection = ea::vector<ea::pair<TextureUnit, Texture*>>;

/// Collection of batches.
class BatchCollection
{
public:
    /// Add group parameter.
    template <class T>
    void AddGroupParameter(StringHash name, const T& value)
    {
        groupParameters_.AddParameter(name, value);
        ++currentGroup_.numParameters_;
    }

    /// Add group texture.
    void AddGroupResource(TextureUnit unit, Texture* texture)
    {
        groupResources_.emplace_back(unit, texture);
        ++currentGroup_.numResources_;
    }

    /// Add instance parameter.
    template <class T>
    void AddInstanceParameter(StringHash name, const T& value)
    {
        instanceParameters_.AddParameter(name, value);
        ++currentInstance_.numParameters_;
    }

    /// Commit instance.
    void CommitInstance()
    {
        instances_.push_back(currentInstance_);
        currentInstance_ = {};
        currentInstance_.parameterOffset_ = instanceParameters_.GetNextParameterOffset();
    }

    /// Commit group.
    void CommitGroup()
    {
        groups_.push_back(currentGroup_);
        currentGroup_ = {};
        currentGroup_.parameterOffset_ = groupParameters_.GetNextParameterOffset();
        currentGroup_.instanceOffset_ = instances_.size();
        currentGroup_.resourceOffset_ = groupResources_.size();
    }

private:
    /// Group description.
    struct GroupDesc
    {
        /// Group parameter offset.
        unsigned parameterOffset_{};
        /// Number of group parameters.
        unsigned numParameters_{};
        /// Group resource offset.
        unsigned resourceOffset_{};
        /// Number of group resources.
        unsigned numResources_{};
        /// Instance offset.
        unsigned instanceOffset_{};
        /// Number of instances.
        unsigned numInstances_{};
    };

    /// Instance description.
    struct InstanceDesc
    {
        /// Parameter offset.
        unsigned parameterOffset_{};
        /// Number of parameters.
        unsigned numParameters_{};
    };

public: // TODO: Remove this hack
    /// Max number of per-instance elements.
    unsigned maxPerInstanceElements_{};
    /// Batch groups.
    ea::vector<GroupDesc> groups_;
    /// Instances.
    ea::vector<InstanceDesc> instances_;

    /// Group parameters.
    ShaderParameterCollection groupParameters_;
    /// Group resources.
    ShaderResourceCollection groupResources_;
    /// Instance parameters.
    ShaderParameterCollection instanceParameters_;

    /// Current group.
    GroupDesc currentGroup_;
    /// Current instance.
    InstanceDesc currentInstance_;
};

Vector4 GetCameraDepthModeParameter(const Camera* camera)
{
    Vector4 depthMode = Vector4::ZERO;
    if (camera->IsOrthographic())
    {
        depthMode.x_ = 1.0f;
#ifdef URHO3D_OPENGL
        depthMode.z_ = 0.5f;
        depthMode.w_ = 0.5f;
#else
        depthMode.z_ = 1.0f;
#endif
    }
    else
        depthMode.w_ = 1.0f / camera->GetFarClip();
    return depthMode;
}

Vector4 GetCameraDepthReconstructParameter(const Camera* camera)
{
    const float nearClip = camera->GetNearClip();
    const float farClip = camera->GetFarClip();
    return {
        farClip / (farClip - nearClip),
        -nearClip / (farClip - nearClip),
        camera->IsOrthographic() ? 1.0f : 0.0f,
        camera->IsOrthographic() ? 0.0f : 1.0f
    };
}

Matrix4 GetEffectiveCameraViewProj(const Camera* camera)
{
    Matrix4 projection = camera->GetGPUProjection();
#ifdef URHO3D_OPENGL
    auto graphics = camera->GetSubsystem<Graphics>();
    // Add constant depth bias manually to the projection matrix due to glPolygonOffset() inconsistency
    const float constantBias = 2.0f * graphics->GetDepthConstantBias();
    projection.m22_ += projection.m32_ * constantBias;
    projection.m23_ += projection.m33_ * constantBias;
#endif
    return projection * camera->GetView();
}

Vector4 GetZoneFogParameter(const Zone* zone, const Camera* camera)
{
    const float farClip = camera->GetFarClip();
    float fogStart = Min(zone->GetFogStart(), farClip);
    float fogEnd = Min(zone->GetFogEnd(), farClip);
    if (fogStart >= fogEnd * (1.0f - M_LARGE_EPSILON))
        fogStart = fogEnd * (1.0f - M_LARGE_EPSILON);
    const float fogRange = Max(fogEnd - fogStart, M_EPSILON);
    return {
        fogEnd / farClip,
        farClip / fogRange,
        0.0f,
        0.0f
    };
}

void FillGlobalSharedParameters(ShaderParameterCollection& collection,
    const FrameInfo& frameInfo, const Camera* camera, const Zone* zone, const Scene* scene)
{
    collection.AddParameter(VSP_DELTATIME, frameInfo.timeStep_);
    collection.AddParameter(PSP_DELTATIME, frameInfo.timeStep_);

    const float elapsedTime = scene->GetElapsedTime();
    collection.AddParameter(VSP_ELAPSEDTIME, elapsedTime);
    collection.AddParameter(PSP_ELAPSEDTIME, elapsedTime);

    const Matrix3x4 cameraEffectiveTransform = camera->GetEffectiveWorldTransform();
    collection.AddParameter(VSP_CAMERAPOS, cameraEffectiveTransform.Translation());
    collection.AddParameter(VSP_VIEWINV, cameraEffectiveTransform);
    collection.AddParameter(VSP_VIEW, camera->GetView());
    collection.AddParameter(PSP_CAMERAPOS, cameraEffectiveTransform.Translation());

    const float nearClip = camera->GetNearClip();
    const float farClip = camera->GetFarClip();
    collection.AddParameter(VSP_NEARCLIP, nearClip);
    collection.AddParameter(VSP_FARCLIP, farClip);
    collection.AddParameter(PSP_NEARCLIP, nearClip);
    collection.AddParameter(PSP_FARCLIP, farClip);

    collection.AddParameter(VSP_DEPTHMODE, GetCameraDepthModeParameter(camera));
    collection.AddParameter(PSP_DEPTHRECONSTRUCT, GetCameraDepthReconstructParameter(camera));

    Vector3 nearVector, farVector;
    camera->GetFrustumSize(nearVector, farVector);
    collection.AddParameter(VSP_FRUSTUMSIZE, farVector);

    collection.AddParameter(VSP_VIEWPROJ, GetEffectiveCameraViewProj(camera));

    collection.AddParameter(VSP_AMBIENTSTARTCOLOR, Color::WHITE);
    collection.AddParameter(VSP_AMBIENTENDCOLOR, Vector4::ZERO);
    collection.AddParameter(VSP_ZONE, Matrix3x4::IDENTITY);
    collection.AddParameter(PSP_AMBIENTCOLOR, Color::WHITE);
    collection.AddParameter(PSP_FOGCOLOR, zone->GetFogColor());
    collection.AddParameter(PSP_FOGPARAMS, GetZoneFogParameter(zone, camera));
}

void ApplyShaderResources(Graphics* graphics, const ShaderResourceCollection& resources)
{
    for (const auto& item : resources)
    {
        if (graphics->HasTextureUnit(item.first))
            graphics->SetTexture(item.first, item.second);
    }
}

}

CustomView::CustomView(Context* context, CustomViewportScript* script)
    : Object(context)
    , graphics_(context_->GetGraphics())
    , workQueue_(context_->GetWorkQueue())
    , script_(script)
{}

CustomView::~CustomView()
{
}

bool CustomView::Define(RenderSurface* renderTarget, Viewport* viewport)
{
    scene_ = viewport->GetScene();
    camera_ = scene_ ? viewport->GetCamera() : nullptr;
    octree_ = scene_ ? scene_->GetComponent<Octree>() : nullptr;
    if (!camera_ || !octree_)
        return false;

    numDrawables_ = octree_->GetAllDrawables().size();
    renderTarget_ = renderTarget;
    viewport_ = viewport;
    return true;
}

void CustomView::Update(const FrameInfo& frameInfo)
{
    frameInfo_ = frameInfo;
    frameInfo_.camera_ = camera_;
}

void CustomView::PostTask(std::function<void(unsigned)> task)
{
    workQueue_->AddWorkItem(ea::move(task), M_MAX_UNSIGNED);
}

void CustomView::CompleteTasks()
{
    workQueue_->Complete(M_MAX_UNSIGNED);
}

/*void CustomView::ClearViewport(ClearTargetFlags flags, const Color& color, float depth, unsigned stencil)
{
    graphics_->Clear(flags, color, depth, stencil);
}*/

void CustomView::CollectDrawables(DrawableCollection& drawables, Camera* camera, DrawableFlags flags)
{
    FrustumOctreeQuery query(drawables, camera->GetFrustum(), flags, camera->GetViewMask());
    octree_->GetDrawables(query);
}

void CustomView::ProcessPrimaryDrawables(DrawableViewportCache& viewportCache,
    const DrawableCollection& drawables, Camera* camera)
{
    // Reset cache
    viewportCache.visibleGeometries_.Clear(numThreads_);
    viewportCache.visibleLights_.Clear(numThreads_);
    viewportCache.sceneZRange_.Clear(numThreads_);
    viewportCache.transient_.Reset(numDrawables_);

    // Prepare frame info
    FrameInfo frameInfo = frameInfo_;
    frameInfo.camera_ = camera;

    // Process drawables
    const unsigned drawablesPerItem = (drawables.size() + numThreads_ - 1) / numThreads_;
    for (unsigned workItemIndex = 0; workItemIndex < numThreads_; ++workItemIndex)
    {
        const unsigned fromIndex = workItemIndex * drawablesPerItem;
        const unsigned toIndex = ea::min((workItemIndex + 1) * drawablesPerItem, drawables.size());

        workQueue_->AddWorkItem([&, camera, fromIndex, toIndex](unsigned threadIndex)
        {
            const DrawableZRangeEvaluator zRangeEvaluator{ camera };

            for (unsigned i = fromIndex; i < toIndex; ++i)
            {
                // TODO: Add occlusion culling
                Drawable* drawable = drawables[i];
                drawable->UpdateBatches(frameInfo);
                ProcessPrimaryDrawable(drawable, zRangeEvaluator, viewportCache, threadIndex);
            }
        }, M_MAX_UNSIGNED);
    }
    workQueue_->Complete(M_MAX_UNSIGNED);
}

void CustomView::CollectLitGeometries(const DrawableViewportCache& viewportCache,
    DrawableLightCache& lightCache, Light* light)
{
    switch (light->GetLightType())
    {
    case LIGHT_SPOT:
    {
        SpotLightLitGeometriesQuery query(lightCache.litGeometries_, viewportCache.transient_, light);
        octree_->GetDrawables(query);
        break;
    }
    case LIGHT_POINT:
    {
        PointLightLitGeometriesQuery query(lightCache.litGeometries_, viewportCache.transient_, light);
        octree_->GetDrawables(query);
        break;
    }
    case LIGHT_DIRECTIONAL:
    {
        const unsigned lightMask = light->GetLightMask();
        viewportCache.visibleGeometries_.ForEach([&](unsigned index, Drawable* drawable)
        {
            if (drawable->GetLightMask() & lightMask)
                lightCache.litGeometries_.push_back(drawable);
        });
        break;
    }
    }
}

void CustomView::Render()
{
    graphics_->SetRenderTarget(0, renderTarget_);
    graphics_->Clear(CLEAR_COLOR | CLEAR_DEPTH | CLEAR_DEPTH, Color::RED * 0.5f);

    script_->Render(this);

    // Collect and process visible drawables
    DrawableCollection drawablesInMainCamera;
    CollectDrawables(drawablesInMainCamera, camera_, DRAWABLE_GEOMETRY | DRAWABLE_LIGHT);

    DrawableViewportCache viewportCache;
    ProcessPrimaryDrawables(viewportCache, drawablesInMainCamera, camera_);

    // Process visible lights
    ea::vector<DrawableLightCache> globalLightCache;
    globalLightCache.resize(viewportCache.visibleLights_.Size());
    viewportCache.visibleLights_.ForEach([&](unsigned lightIndex, Light* light)
    {
        PostTask([&, this, light, lightIndex](unsigned threadIndex)
        {
            DrawableLightCache& lightCache = globalLightCache[lightIndex];
            CollectLitGeometries(viewportCache, lightCache, light);
        });
    });
    CompleteTasks();

    // Accumulate light
    ea::vector<DrawableLightAccumulator<4>> lightAccumulator;
    lightAccumulator.resize(numDrawables_);

    viewportCache.visibleLights_.ForEach([&](unsigned lightIndex, Light* light)
    {
        DrawableLightAccumulatorContext ctx;
        ctx.maxPixelLights_ = 1;

        DrawableLightCache& lightCache = globalLightCache[lightIndex];
        for (Drawable* litGeometry : lightCache.litGeometries_)
        {
            const unsigned drawableIndex = litGeometry->GetDrawableIndex();
            const float lightPenalty = GetLightPenalty(light, litGeometry);
            lightAccumulator[drawableIndex].AccumulateLight(ctx, lightPenalty, light);
        }
    });

    // Collect batches
    ShaderParameterCollection globalSharedParameters;
    FillGlobalSharedParameters(globalSharedParameters, frameInfo_, camera_, octree_->GetZone(), scene_);

    auto renderer = context_->GetRenderer();
    viewportCache.visibleGeometries_.ForEach([&](unsigned index, Drawable* drawable)
    {
        for (const SourceBatch& sourceBatch : drawable->GetBatches())
        {
            auto tech = sourceBatch.material_->GetTechnique(0);
            auto pass = tech->GetSupportedPass(0);
            SphericalHarmonicsDot9 sh;

            BatchCollection batchCollection;
            if (sourceBatch.material_)
            {
                const auto& parameters = sourceBatch.material_->GetShaderParameters();
                for (const auto& item : parameters)
                    batchCollection.AddGroupParameter(item.first, item.second.value_);

                const auto& textures = sourceBatch.material_->GetTextures();
                for (const auto& item : textures)
                    batchCollection.AddGroupResource(item.first, item.second);
            }

            batchCollection.AddInstanceParameter(VSP_SHAR, sh.Ar_);
            batchCollection.AddInstanceParameter(VSP_SHAG, sh.Ag_);
            batchCollection.AddInstanceParameter(VSP_SHAB, sh.Ab_);
            batchCollection.AddInstanceParameter(VSP_SHBR, sh.Br_);
            batchCollection.AddInstanceParameter(VSP_SHBG, sh.Bg_);
            batchCollection.AddInstanceParameter(VSP_SHBB, sh.Bb_);
            batchCollection.AddInstanceParameter(VSP_SHC, sh.C_);
            batchCollection.AddInstanceParameter(VSP_MODEL, *sourceBatch.worldTransform_);
            batchCollection.CommitInstance();
            batchCollection.CommitGroup();

            //PipelineStateDesc pipelineStateDesc;
            //pipelineStateDesc.vertexElements_.append(sourceBatch.geometry_->getve);

            auto vertexShader = graphics_->GetShader(VS, pass->GetVertexShader(), pass->GetEffectiveVertexShaderDefines());
            auto pixelShader = graphics_->GetShader(PS, pass->GetPixelShader(), pass->GetEffectivePixelShaderDefines());

            graphics_->SetShaders(vertexShader, pixelShader);
            graphics_->SetBlendMode(BLEND_REPLACE, false);
            renderer->SetCullMode(sourceBatch.material_->GetCullMode(), camera_);
            graphics_->SetFillMode(FILL_SOLID);
            graphics_->SetDepthTest(CMP_LESSEQUAL);
            graphics_->SetDepthWrite(true);

            globalSharedParameters.ForEach(SharedParameterSetter{ graphics_ });
            batchCollection.groupParameters_.ForEach(SharedParameterSetter{ graphics_ });
            batchCollection.instanceParameters_.ForEach(SharedParameterSetter{ graphics_ });
            ApplyShaderResources(graphics_, batchCollection.groupResources_);

            sourceBatch.geometry_->Draw(graphics_);
        }
    });

    return;

#if 0
    //auto graphics = context_->GetGraphics();
    BatchQueue queue;
    viewportCache.visibleGeometries_.ForEach([&](unsigned index, Drawable* drawable)
    {
            for (const SourceBatch& sourceBatch : drawable->GetBatches())
            {
                Batch destBatch(sourceBatch);
                auto tech = destBatch.material_->GetTechnique(0);
                destBatch.zone_ = renderer->GetDefaultZone();
                destBatch.pass_ = tech->GetSupportedPass(0);
                destBatch.isBase_ = true;
                destBatch.lightMask_ = 0xffffffff;
                //renderer->SetBatchShaders(destBatch, tech, false, queue);
                destBatch.vertexShader_ = graphics_->GetShader(VS, destBatch.pass_->GetVertexShader(), destBatch.pass_->GetEffectiveVertexShaderDefines());
                destBatch.pixelShader_ = graphics_->GetShader(PS, destBatch.pass_->GetPixelShader(), destBatch.pass_->GetEffectivePixelShaderDefines());

                destBatch.CalculateSortKey();
                //queue.batches_.push_back(destBatch);

                //view->SetGlobalShaderParameters();
                graphics_->SetShaderParameter(VSP_DELTATIME, frameInfo_.timeStep_);
                graphics_->SetShaderParameter(PSP_DELTATIME, frameInfo_.timeStep_);

                if (scene_)
                {
                    float elapsedTime = scene_->GetElapsedTime();
                    graphics_->SetShaderParameter(VSP_ELAPSEDTIME, elapsedTime);
                    graphics_->SetShaderParameter(PSP_ELAPSEDTIME, elapsedTime);
                }

                graphics_->SetShaders(destBatch.vertexShader_, destBatch.pixelShader_);
                graphics_->SetBlendMode(BLEND_REPLACE, false);
                renderer->SetCullMode(destBatch.material_->GetCullMode(), camera_);
                graphics_->SetFillMode(FILL_SOLID);
                graphics_->SetDepthTest(CMP_LESSEQUAL);
                graphics_->SetDepthWrite(true);
                if (graphics_->NeedParameterUpdate(SP_CAMERA, camera_))
                {
                    //view->SetCameraShaderParameters(camera_);
                        Matrix3x4 cameraEffectiveTransform = camera_->GetEffectiveWorldTransform();

    graphics_->SetShaderParameter(VSP_CAMERAPOS, cameraEffectiveTransform.Translation());
    graphics_->SetShaderParameter(VSP_VIEWINV, cameraEffectiveTransform);
    graphics_->SetShaderParameter(VSP_VIEW, camera_->GetView());
    graphics_->SetShaderParameter(PSP_CAMERAPOS, cameraEffectiveTransform.Translation());

    float nearClip = camera_->GetNearClip();
    float farClip = camera_->GetFarClip();
    graphics_->SetShaderParameter(VSP_NEARCLIP, nearClip);
    graphics_->SetShaderParameter(VSP_FARCLIP, farClip);
    graphics_->SetShaderParameter(PSP_NEARCLIP, nearClip);
    graphics_->SetShaderParameter(PSP_FARCLIP, farClip);

    Vector4 depthMode = Vector4::ZERO;
    if (camera_->IsOrthographic())
    {
        depthMode.x_ = 1.0f;
#ifdef URHO3D_OPENGL
        depthMode.z_ = 0.5f;
        depthMode.w_ = 0.5f;
#else
        depthMode.z_ = 1.0f;
#endif
    }
    else
        depthMode.w_ = 1.0f / camera_->GetFarClip();

    graphics_->SetShaderParameter(VSP_DEPTHMODE, depthMode);

    Vector4 depthReconstruct
        (farClip / (farClip - nearClip), -nearClip / (farClip - nearClip), camera_->IsOrthographic() ? 1.0f : 0.0f,
            camera_->IsOrthographic() ? 0.0f : 1.0f);
    graphics_->SetShaderParameter(PSP_DEPTHRECONSTRUCT, depthReconstruct);

    Vector3 nearVector, farVector;
    camera_->GetFrustumSize(nearVector, farVector);
    graphics_->SetShaderParameter(VSP_FRUSTUMSIZE, farVector);

    Matrix4 projection = camera_->GetGPUProjection();
#ifdef URHO3D_OPENGL
    // Add constant depth bias manually to the projection matrix due to glPolygonOffset() inconsistency
    float constantBias = 2.0f * graphics_->GetDepthConstantBias();
    projection.m22_ += projection.m32_ * constantBias;
    projection.m23_ += projection.m33_ * constantBias;
#endif

    graphics_->SetShaderParameter(VSP_VIEWPROJ, projection * camera_->GetView());
                    // During renderpath commands the G-Buffer or viewport texture is assumed to always be viewport-sized
                    //view->SetGBufferShaderParameters(viewSize, IntRect(0, 0, viewSize.x_, viewSize.y_));
                }
                graphics_->SetShaderParameter(VSP_SHAR, destBatch.shaderParameters_.ambient_.Ar_);
                graphics_->SetShaderParameter(VSP_SHAG, destBatch.shaderParameters_.ambient_.Ag_);
                graphics_->SetShaderParameter(VSP_SHAB, destBatch.shaderParameters_.ambient_.Ab_);
                graphics_->SetShaderParameter(VSP_SHBR, destBatch.shaderParameters_.ambient_.Br_);
                graphics_->SetShaderParameter(VSP_SHBG, destBatch.shaderParameters_.ambient_.Bg_);
                graphics_->SetShaderParameter(VSP_SHBB, destBatch.shaderParameters_.ambient_.Bb_);
                graphics_->SetShaderParameter(VSP_SHC, destBatch.shaderParameters_.ambient_.C_);
                graphics_->SetShaderParameter(VSP_MODEL, *destBatch.worldTransform_);
                //graphics_->SetShaderParameter(VSP_AMBIENTSTARTCOLOR, destBatch.zone_->GetAmbientStartColor());
                graphics_->SetShaderParameter(VSP_AMBIENTSTARTCOLOR, Color::WHITE);
                graphics_->SetShaderParameter(VSP_AMBIENTENDCOLOR, Vector4::ZERO);
                graphics_->SetShaderParameter(VSP_ZONE, Matrix3x4::IDENTITY);
                //graphics_->SetShaderParameter(PSP_AMBIENTCOLOR, destBatch.zone_->GetAmbientColor());
                graphics_->SetShaderParameter(PSP_AMBIENTCOLOR, Color::WHITE);
                graphics_->SetShaderParameter(PSP_FOGCOLOR, destBatch.zone_->GetFogColor());

                float farClip = camera_->GetFarClip();
                float fogStart = Min(destBatch.zone_->GetFogStart(), farClip);
                float fogEnd = Min(destBatch.zone_->GetFogEnd(), farClip);
                if (fogStart >= fogEnd * (1.0f - M_LARGE_EPSILON))
                    fogStart = fogEnd * (1.0f - M_LARGE_EPSILON);
                float fogRange = Max(fogEnd - fogStart, M_EPSILON);
                Vector4 fogParams(fogEnd / farClip, farClip / fogRange, 0.0f, 0.0f);

                graphics_->SetShaderParameter(PSP_FOGPARAMS, fogParams);

                // Set material-specific shader parameters and textures
                if (destBatch.material_)
                {
                    if (graphics_->NeedParameterUpdate(SP_MATERIAL, reinterpret_cast<const void*>(destBatch.material_->GetShaderParameterHash())))
                    {
                        const ea::unordered_map<StringHash, MaterialShaderParameter>& parameters = destBatch.material_->GetShaderParameters();
                        for (auto i = parameters.begin(); i !=
                            parameters.end(); ++i)
                            graphics_->SetShaderParameter(i->first, i->second.value_);
                    }

                    const ea::unordered_map<TextureUnit, SharedPtr<Texture> >& textures = destBatch.material_->GetTextures();
                    for (auto i = textures.begin(); i !=
                        textures.end(); ++i)
                    {
                        if (i->first == TU_EMISSIVE && destBatch.lightmapScaleOffset_)
                            continue;

                        if (graphics_->HasTextureUnit(i->first))
                            graphics_->SetTexture(i->first, i->second.Get());
                    }

                    /*if (destBatch.lightmapScaleOffset_)
                    {
                        if (Scene* scene = view->GetScene())
                            graphics_->SetTexture(TU_EMISSIVE, scene->GetLightmapTexture(destBatch.lightmapIndex_));
                    }*/
                    destBatch.geometry_->Draw(graphics_);
                }
            }
    });
#endif
}

}
