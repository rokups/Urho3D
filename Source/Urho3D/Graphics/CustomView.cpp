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
#include "../Graphics/IndexBuffer.h"
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
#include "../Graphics/PipelineState.h"
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
    /// Container for vertex lights.
    using VertexLightContainer = ea::array<Light*, MaxVertexLights>;

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
        firstVertexLight_ = ea::max(ctx.maxPixelLights_, numImportantLights_);
        const unsigned maxLights = MaxVertexLights + firstVertexLight_;
        if (lights_.size() > maxLights)
        {
            // TODO: Update SH
            lights_.pop_back();
        }
    }

    /// Return main directional per-pixel light.
    Light* GetMainDirectionalLight() const
    {
        return lights_.empty() ? nullptr : lights_.front().second;
    }

    /// Return per-vertex lights.
    VertexLightContainer GetVertexLights() const
    {
        VertexLightContainer vertexLights;
        for (unsigned i = firstVertexLight_; i < lights_.size(); ++i)
            vertexLights[i - firstVertexLight_] = lights_.at(i).second;
        return vertexLights;
    }

    /// Container of per-pixel and per-pixel lights.
    Container lights_;
    /// Accumulated SH lights.
    SphericalHarmonicsDot9 sh_;
    /// Number of important lights.
    unsigned numImportantLights_{};
    /// First vertex light.
    unsigned firstVertexLight_{};
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

CullMode GetEffectiveCullMode(CullMode mode, const Camera* camera)
{
    // If a camera is specified, check whether it reverses culling due to vertical flipping or reflection
    if (camera && camera->GetReverseCulling())
    {
        if (mode == CULL_CW)
            mode = CULL_CCW;
        else if (mode == CULL_CCW)
            mode = CULL_CW;
    }

    return mode;
}

void ApplyPipelineState(Graphics* graphics, const PipelineStateDesc& desc)
{
    graphics->SetShaders(desc.vertexShader_, desc.pixelShader_);
    graphics->SetDepthWrite(desc.depthWrite_);
    graphics->SetDepthTest(desc.depthMode_);
    graphics->SetStencilTest(desc.stencilEnabled_, desc.stencilMode_,
        desc.stencilPass_, desc.stencilFail_, desc.stencilDepthFail_,
        desc.stencilRef_, desc.compareMask_, desc.writeMask_);

    graphics->SetColorWrite(desc.colorWrite_);
    graphics->SetBlendMode(desc.blendMode_, desc.alphaToCoverage_);

    graphics->SetFillMode(desc.fillMode_);
    graphics->SetCullMode(desc.cullMode_);
    graphics->SetDepthBias(desc.constantDepthBias_, desc.slopeScaledDepthBias_);
};

/// Pipeline state cache.
class PipelineStateCache : public Object
{
    URHO3D_OBJECT(PipelineStateCache, Object);

public:
    /// Construct.
    explicit PipelineStateCache(Context* context) : Object(context) {}
    /// Create new or return existing pipeline state.
    PipelineState* GetPipelineState(const PipelineStateDesc& desc)
    {
        SharedPtr<PipelineState>& state = states_[desc];
        if (!state)
        {
            state = MakeShared<PipelineState>(context_);
            state->Create(desc);
        }
        return state;
    }

private:
    /// Cached states.
    ea::unordered_map<PipelineStateDesc, SharedPtr<PipelineState>> states_;
};

struct BatchPipelineStateKey
{
    /// Geometry to be rendered.
    Geometry* geometry_{};
    /// Material to be rendered.
    Material* material_{};
    /// Pass of the material technique to be used.
    Pass* pass_{};
    /// Light to be applied.
    Light* light_{};

    /// Compare.
    bool operator ==(const BatchPipelineStateKey& rhs) const
    {
        return geometry_ == rhs.geometry_
            && material_ == rhs.material_
            && pass_ == rhs.pass_
            && light_ == rhs.light_;
    }

    /// Return hash.
    unsigned ToHash() const
    {
        unsigned hash = 0;
        CombineHash(hash, MakeHash(geometry_));
        CombineHash(hash, MakeHash(material_));
        CombineHash(hash, MakeHash(pass_));
        CombineHash(hash, MakeHash(light_));
        return hash;
    }
};

struct BatchPipelineState
{
    /// Pipeline state.
    SharedPtr<PipelineState> pipelineState_;
    /// Cached state of the geometry.
    unsigned geometryHash_{};
    /// Cached state of the material.
    unsigned materialHash_{};
    /// Cached state of the pass.
    unsigned passHash_{};
    // TODO: Hash light too
};

class BatchPipelineStateCache
{
public:
    BatchPipelineStateCache(PipelineStateCache& cache)
        : graphics_(cache.GetContext()->GetGraphics())
        , underlyingCache_(&cache)
    {}

    PipelineState* GetPipelineState(const BatchPipelineStateKey& key, Camera* camera)
    {
        Geometry* geometry = key.geometry_;
        Material* material = key.material_;
        Pass* pass = key.pass_;

        BatchPipelineState& state = validationMap_[key];
        if (!state.pipelineState_
            || geometry->GetPipelineStateHash() != state.geometryHash_
            || material->GetPipelineStateHash() != state.materialHash_
            || pass->GetPipelineStateHash() != state.passHash_)
        {
            PipelineStateDesc desc;

            for (VertexBuffer* vertexBuffer : geometry->GetVertexBuffers())
                desc.vertexElements_.append(vertexBuffer->GetElements());

            desc.vertexShader_ = graphics_->GetShader(
                VS, pass->GetVertexShader(), pass->GetEffectiveVertexShaderDefines());
            desc.pixelShader_ = graphics_->GetShader(
                PS, pass->GetPixelShader(), pass->GetEffectivePixelShaderDefines());

            desc.primitiveType_ = geometry->GetPrimitiveType();
            if (auto indexBuffer = geometry->GetIndexBuffer())
                desc.indexType_ = indexBuffer->GetIndexSize() == 2 ? IBT_UINT16 : IBT_UINT32;

            desc.depthWrite_ = true;
            desc.depthMode_ = CMP_LESSEQUAL;
            desc.stencilEnabled_ = false;
            desc.stencilMode_ = CMP_ALWAYS;

            desc.colorWrite_ = true;
            desc.blendMode_ = BLEND_REPLACE;
            desc.alphaToCoverage_ = false;

            desc.fillMode_ = FILL_SOLID;
            desc.cullMode_ = GetEffectiveCullMode(material->GetCullMode(), camera);

            state.pipelineState_ = underlyingCache_->GetPipelineState(desc);
            state.geometryHash_ = key.geometry_->GetPipelineStateHash();
            state.materialHash_ = key.material_->GetPipelineStateHash();
            state.passHash_ = key.pass_->GetPipelineStateHash();
        }
        return state.pipelineState_;
    }

private:
    Graphics* graphics_{};
    PipelineStateCache* underlyingCache_{};
    ea::unordered_map<BatchPipelineStateKey, BatchPipelineState> validationMap_;
};


struct ForwardBaseBatchSortKey
{
    /// Geometry to be rendered.
    Geometry* geometry_{};
    /// Material to be rendered.
    Material* material_{};
    /// Pipeline state.
    PipelineState* pipelineState_{};
    /// Hash of used shaders.
    unsigned shadersHash_{};
    /// Distance from camera.
    float distance_{};
    /// 8-bit render order modifier from material.
    unsigned char renderOrder_{};
};

struct ForwardBaseBatch : public ForwardBaseBatchSortKey
{
    /// Drawable to be rendered.
    Drawable* drawable_;
    /// Source batch of the drawable.
    const SourceBatch* sourceBatch_;
    /// Main per-pixel directional light.
    Light* mainDirectionalLight_{};
    /// Array of per-vertex lights.
    ea::array<Light*, 4> vertexLights_{};
    /// Accumulated SH lighting.
    SphericalHarmonicsDot9 shLighting_;
};

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
    static DrawableCollection drawablesInMainCamera;
    drawablesInMainCamera.clear();
    CollectDrawables(drawablesInMainCamera, camera_, DRAWABLE_GEOMETRY | DRAWABLE_LIGHT);

    static DrawableViewportCache viewportCache;
    ProcessPrimaryDrawables(viewportCache, drawablesInMainCamera, camera_);

    // Process visible lights
    static ea::vector<DrawableLightCache> globalLightCache;
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
    static ea::vector<DrawableLightAccumulator<4>> lightAccumulator;
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

    //
    static PipelineStateCache pipelineStateCache(context_);
    static BatchPipelineStateCache batchPipelineStateCache(pipelineStateCache);

    // Collect intermediate batches
    static ea::vector<ForwardBaseBatch> forwardBaseBatches;
    forwardBaseBatches.clear();
    viewportCache.visibleGeometries_.ForEach([&](unsigned index, Drawable* drawable)
    {
        const unsigned drawableIndex = drawable->GetDrawableIndex();
        const auto& drawableLights = lightAccumulator[drawableIndex];
        for (const SourceBatch& sourceBatch : drawable->GetBatches())
        {
            ForwardBaseBatch baseBatch;
            baseBatch.geometry_ = sourceBatch.geometry_;
            baseBatch.material_ = sourceBatch.material_;
            baseBatch.distance_ = sourceBatch.distance_;
            baseBatch.renderOrder_ = sourceBatch.material_->GetRenderOrder();

            baseBatch.drawable_ = drawable;
            baseBatch.sourceBatch_ = &sourceBatch;

            baseBatch.mainDirectionalLight_ = drawableLights.GetMainDirectionalLight();
            baseBatch.vertexLights_ = drawableLights.GetVertexLights();

            BatchPipelineStateKey pipelineStateKey;
            pipelineStateKey.geometry_ = baseBatch.geometry_;
            pipelineStateKey.material_ = baseBatch.material_;
            pipelineStateKey.pass_ = baseBatch.material_->GetTechnique(0)->GetSupportedPass(0);
            pipelineStateKey.light_ = baseBatch.mainDirectionalLight_;

            baseBatch.pipelineState_ = batchPipelineStateCache.GetPipelineState(pipelineStateKey, camera_);
            baseBatch.shadersHash_ = 0;

            forwardBaseBatches.push_back(baseBatch);
        }
    });

    // Collect batches
    ShaderParameterCollection globalSharedParameters;
    FillGlobalSharedParameters(globalSharedParameters, frameInfo_, camera_, octree_->GetZone(), scene_);

    auto renderer = context_->GetRenderer();
    for (const ForwardBaseBatch& batch : forwardBaseBatches)
    {
        auto geometry = batch.geometry_;
        const SourceBatch& sourceBatch = *batch.sourceBatch_;
        SphericalHarmonicsDot9 sh;
        BatchCollection batchCollection;
        if (batch.material_)
        {
            const auto& parameters = batch.material_->GetShaderParameters();
            for (const auto& item : parameters)
                batchCollection.AddGroupParameter(item.first, item.second.value_);

            const auto& textures = batch.material_->GetTextures();
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

        ApplyPipelineState(graphics_, batch.pipelineState_->GetDesc());

        globalSharedParameters.ForEach(SharedParameterSetter{ graphics_ });
        batchCollection.groupParameters_.ForEach(SharedParameterSetter{ graphics_ });
        batchCollection.instanceParameters_.ForEach(SharedParameterSetter{ graphics_ });
        ApplyShaderResources(graphics_, batchCollection.groupResources_);

        sourceBatch.geometry_->Draw(graphics_);
    }
}

}
