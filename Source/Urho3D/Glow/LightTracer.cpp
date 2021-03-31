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

#include "../Glow/Helpers.h"
#include "../Glow/RaytracerScene.h"
#include "../Glow/LightTracer.h"
#include "../Graphics/LightProbeGroup.h"
#include "../IO/Log.h"
#include "../Math/TetrahedralMesh.h"

#include <embree3/rtcore.h>
#include <embree3/rtcore_ray.h>

#include <future>

using namespace embree3;

namespace Urho3D
{

namespace
{

/// Calculate bias scale based on position.
float CalculateBiasScale(const Vector3& position)
{
    return ea::min(1.0f, ea::max({ Abs(position.x_), Abs(position.y_), Abs(position.z_) }));
}

/// Generate random 3D direction.
void RandomDirection3(Vector3& result)
{
    float len;

    do
    {
        result.x_ = Random(-1.0f, 1.0f);
        result.y_ = Random(-1.0f, 1.0f);
        result.z_ = Random(-1.0f, 1.0f);
        len = result.Length();

    } while (len > 1.0f);

    result /= len;
}

/// Generate random offset within 2D circle.
Vector2 RandomCircleOffset()
{
    Vector2 result;
    float len;

    do
    {
        result.x_ = Random(-1.0f, 1.0f);
        result.y_ = Random(-1.0f, 1.0f);
        len = result.Length();

    } while (len > 1.0f);

    return result;
}

/// Generate random hemisphere direction.
Vector3 RandomHemisphereDirection(const Vector3& normal)
{
    Vector3 result;
    RandomDirection3(result);

    if (result.DotProduct(normal) < 0)
        result = -result;

    return result;
}

/// Return number of samples to use for light.
unsigned CalculateNumSamples(const BakedLight& light, unsigned maxSamples)
{
    switch (light.lightType_)
    {
    case LIGHT_DIRECTIONAL:
        return light.angle_ < M_LARGE_EPSILON ? 1 : maxSamples;
    default:
        return light.radius_ < M_LARGE_EPSILON ? 1 : maxSamples;
    }
}

/// Return true if first geometry is non-primary LOD of another geometry or different LOD of itself.
bool IsUnwantedLod(const RaytracerGeometry& currentGeometry, const RaytracerGeometry& hitGeometry)
{
    const bool hitLod = hitGeometry.lodIndex_ != 0;
    const bool sameGeometry = currentGeometry.objectIndex_ == hitGeometry.objectIndex_
        && currentGeometry.geometryIndex_ == hitGeometry.geometryIndex_;

    const bool hitLodOfAnotherGeometry = !sameGeometry && hitLod;
    const bool hitAnotherLodOfSameGeometry = sameGeometry && hitGeometry.lodIndex_ != currentGeometry.lodIndex_;
    return hitLodOfAnotherGeometry || hitAnotherLodOfSameGeometry;
}

/// Return texture color at hit position. Texture must be present.
Color GetHitDiffuseTextureColor(const RaytracerGeometry& hitGeometry, const RTCHit& hit)
{
    assert(hitGeometry.material_.diffuseImage_);

    Vector2 uv;
    rtcInterpolate0(hitGeometry.embreeGeometry_, hit.primID, hit.u, hit.v,
        RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, RaytracerScene::UVAttribute, &uv.x_, 2);

    return hitGeometry.material_.SampleDiffuse(uv);
}

/// Return true if transparent, update incoming light. Used for direct light calculations.
bool IsTransparedForDirect(const RaytracerGeometry& hitGeometry, const RTCHit& hit, Vector3& incomingLight)
{
    if (hitGeometry.material_.opaque_)
        return false;

    // Consider material
    Vector3 hitSurfaceColor = hitGeometry.material_.diffuseColor_;
    float hitSurfaceAlpha = hitGeometry.material_.alpha_;

    // Consider texture
    if (hitGeometry.material_.diffuseImage_)
    {
        const Color diffuseColor = GetHitDiffuseTextureColor(hitGeometry, hit);

        hitSurfaceColor *= diffuseColor.ToVector3();
        hitSurfaceAlpha *= diffuseColor.a_;
    }

    const float transparency = Clamp(1.0f - hitSurfaceAlpha, 0.0f, 1.0f);
    const float filterIntensity = 1.0f - transparency;
    incomingLight *= transparency * Lerp(Vector3::ONE, hitSurfaceColor, filterIntensity);
    return true;
}

/// Return true if transparent. Used for indirect light calculations.
bool IsTransparentForIndirect(const RaytracerGeometry& hitGeometry, const RTCHit& hit)
{
    if (hitGeometry.material_.opaque_)
        return false;

    const float sample = Random(1.0f);

    // Consider material
    float hitSurfaceAlpha = hitGeometry.material_.alpha_;
    if (hitSurfaceAlpha < sample)
        return true;

    // Consider texture
    if (hitGeometry.material_.diffuseImage_)
    {
        hitSurfaceAlpha *= GetHitDiffuseTextureColor(hitGeometry, hit).a_;
        if (hitSurfaceAlpha < sample)
            return true;
    }

    return false;
}

/// Ray tracing context for geometry buffer preprocessing.
struct GeometryBufferPreprocessContext : public RTCIntersectContext
{
    /// Current geometry.
    const RaytracerGeometry* currentGeometry_{};
    /// Geometry index.
    const ea::vector<RaytracerGeometry>* geometryIndex_{};
};

/// Filter function for geometry buffer preprocessing.
void GeometryBufferPreprocessFilter(const RTCFilterFunctionNArguments* args)
{
    const auto& ctx = *static_cast<const GeometryBufferPreprocessContext*>(args->context);
    const auto& hit = *reinterpret_cast<RTCHit*>(args->hit);
    assert(args->N == 1);

    // Ignore invalid
    if (args->valid[0] == 0)
        return;

    // Ignore all LODs
    const RaytracerGeometry& hitGeometry = (*ctx.geometryIndex_)[hit.geomID];
    if (IsUnwantedLod(*ctx.currentGeometry_, hitGeometry))
        args->valid[0] = 0;
}

/// Base context for direct light tracing.
struct DirectTracingContextBase : public RTCIntersectContext
{
    /// Incoming light accumulator.
    Vector3* incomingLight_{};
};

/// Ray tracing context for direct light baking for charts.
struct DirectTracingContextForCharts : public DirectTracingContextBase
{
    /// Current geometry.
    const RaytracerGeometry* currentGeometry_{};
    /// Geometry index.
    const ea::vector<RaytracerGeometry>* geometryIndex_{};
};

/// Filter function for direct light baking for charts.
void TracingFilterForChartsDirect(const RTCFilterFunctionNArguments* args)
{
    const auto& ctx = *static_cast<const DirectTracingContextForCharts*>(args->context);
    auto& hit = *reinterpret_cast<RTCHit*>(args->hit);
    assert(args->N == 1);

    // Ignore invalid
    if (args->valid[0] == 0)
        return;

    // Ignore if unwanted LOD
    const RaytracerGeometry& hitGeometry = (*ctx.geometryIndex_)[hit.geomID];
    if (IsUnwantedLod(*ctx.currentGeometry_, hitGeometry))
        args->valid[0] = 0;

    // Accumulate and ignore if transparent
    if (IsTransparedForDirect(hitGeometry, hit, *ctx.incomingLight_))
        args->valid[0] = 0;
}

/// Ray tracing context for direct light baking for light probes.
struct DirectTracingContextForLightProbes : public DirectTracingContextBase
{
    /// Geometry index.
    const ea::vector<RaytracerGeometry>* geometryIndex_{};
};

/// Filter function for direct light baking for light probes.
void TracingFilterForLightProbesDirect(const RTCFilterFunctionNArguments* args)
{
    const auto& ctx = *static_cast<const DirectTracingContextForLightProbes*>(args->context);
    auto& hit = *reinterpret_cast<RTCHit*>(args->hit);
    assert(args->N == 1);

    // Ignore invalid
    if (args->valid[0] == 0)
        return;

    // Ignore if LOD
    const RaytracerGeometry& hitGeometry = (*ctx.geometryIndex_)[hit.geomID];
    if (hitGeometry.lodIndex_ != 0)
        args->valid[0] = 0;

    // Accumulate and ignore if transparent
    if (IsTransparedForDirect(hitGeometry, hit, *ctx.incomingLight_))
        args->valid[0] = 0;
}

/// Ray generator for directional light.
struct RayGeneratorForDirectLight
{
    /// Light color.
    Color lightColor_;
    /// Light direction.
    Vector3 lightDirection_;
    /// Light rotation.
    Quaternion lightRotation_;
    /// Max ray distance.
    float maxRayDistance_{};
    /// Tangent of half angle.
    float halfAngleTan_{};

    /// Generate ray for given position. Return true if non-zero.
    bool Generate(const Vector3& /*position*/,
        Vector3& rayOffset, Vector3& lightIntensity, Vector3& lightIncomingDirection)
    {
        const Vector2 randomOffset = RandomCircleOffset() * maxRayDistance_ * halfAngleTan_;
        rayOffset = maxRayDistance_ * lightDirection_;
        rayOffset += Vector3(randomOffset, 0.0f);
        lightIntensity = lightColor_.ToVector3();
        lightIncomingDirection = -lightDirection_;
        return true;
    }
};

/// Ray generator for point light.
struct RayGeneratorForPointLight
{
    /// Light color.
    Color lightColor_;
    /// Light position.
    Vector3 lightPosition_;
    /// Light distance.
    float lightDistance_{};
    /// Light radius.
    float lightRadius_{};

    /// Generate ray for given position. Return true if non-zero.
    bool Generate(const Vector3& position,
        Vector3& rayOffset, Vector3& lightIntensity, Vector3& lightIncomingDirection)
    {
        const Vector2 randomOffset = RandomCircleOffset() * lightRadius_;
        rayOffset = position - lightPosition_;
        rayOffset += Quaternion(Vector3::FORWARD, rayOffset) * Vector3(randomOffset, 0.0f);

        const float distance = rayOffset.Length();
        const float distanceAttenuation = ea::max(0.0f, 1.0f - (distance - lightRadius_) / (lightDistance_ - lightRadius_));

        lightIntensity = lightColor_.ToVector3() * distanceAttenuation * distanceAttenuation;
        lightIncomingDirection = (lightPosition_ - position).Normalized();
        return distanceAttenuation > M_LARGE_EPSILON;
    }
};

/// Ray generator for spot light.
struct RayGeneratorForSpotLight
{
    /// Light color.
    Color lightColor_;
    /// Light position.
    Vector3 lightPosition_;
    /// Light direction.
    Vector3 lightDirection_;
    /// Light rotation.
    Quaternion lightRotation_;
    /// Light distance.
    float lightDistance_{};
    /// Light radius.
    float lightRadius_{};
    /// Light cutoff aka Cos(fov * 0.5f).
    float lightCutoff_{};

    /// Generate ray for given position. Return true if non-zero.
    bool Generate(const Vector3& position,
        Vector3& rayOffset, Vector3& lightIntensity, Vector3& lightIncomingDirection)
    {
        const Vector2 randomOffset = RandomCircleOffset() * lightRadius_;
        rayOffset = position - lightPosition_;
        rayOffset += lightRotation_ * Vector3(randomOffset, 0.0f);

        const float distance = rayOffset.Length();

        const Vector3 rayDirection = rayOffset / distance;
        const float dot = lightDirection_.DotProduct(rayDirection);
        const float invCutoff = 1.0f / (1.0f - lightCutoff_);
        const float spotAttenuation = Clamp((dot - lightCutoff_) * invCutoff, 0.0f, 1.0f);

        const float distanceAttenuation = ea::max(0.0f, 1.0f - (distance - lightRadius_) / (lightDistance_ - lightRadius_));
        lightIntensity = lightColor_.ToVector3() * distanceAttenuation * distanceAttenuation * spotAttenuation;

        lightIncomingDirection = (lightPosition_ - position).Normalized();
        return distanceAttenuation > M_LARGE_EPSILON && spotAttenuation > M_LARGE_EPSILON;
    }
};

/// Direct light tracing for charts: tracing kernel.
struct ChartDirectTracingKernel
{
    /// Indirect light chart.
    LightmapChartBakedDirect* bakedDirect_{};
    /// Geometry buffer.
    const LightmapChartGeometryBuffer* geometryBuffer_{};
    /// Mapping from geometry buffer ID to embree geometry ID.
    const ea::vector<unsigned>* geometryBufferToRaytracer_{};
    /// Raytracer geometries.
    const ea::vector<RaytracerGeometry>* raytracerGeometries_{};
    /// Settings.
    const DirectLightTracingSettings* settings_{};
    /// Indirect brightness multiplier.
    float indirectBrightness_{};
    /// Number of samples.
    unsigned numSamples_{ 1 };
    /// Whether to bake direct light for direct lighting itself.
    bool bakeDirect_{};
    /// Whether to bake direct light for indirect lighting.
    bool bakeIndirect_{};
    unsigned lightMask_{};

    /// Current smooth interpolated normal.
    Vector3 currentSmoothNormal_;
    /// Current geometry ID.
    unsigned currentGeometryId_;

    /// Accumulated light.
    Vector3 accumulatedLight_;

    /// Return number of elements to trace.
    unsigned GetNumElements() const { return bakedDirect_->directLight_.size(); }

    /// Return number of samples.
    unsigned GetNumSamples() const { return numSamples_; }

    /// Return geometry mask to use.
    unsigned GetGeometryMask() const { return RaytracerScene::AllGeometry; }

    /// Return ray intersect context.
    DirectTracingContextForCharts GetRayContext()
    {
        DirectTracingContextForCharts rayContext;
        rtcInitIntersectContext(&rayContext);
        rayContext.geometryIndex_ = raytracerGeometries_;
        rayContext.filter = TracingFilterForChartsDirect;
        return rayContext;
    }

    /// Begin tracing element.
    bool BeginElement(unsigned elementIndex, DirectTracingContextForCharts& rayContext, Vector3& position)
    {
        const unsigned geometryId = geometryBuffer_->geometryIds_[elementIndex];
        const unsigned objectLightMask = geometryBuffer_->lightMasks_[elementIndex];
        if (!geometryId || (objectLightMask & lightMask_) == 0)
            return false;

        // Initialize per-element data in ray context
        const unsigned raytracerGeometryId = (*geometryBufferToRaytracer_)[geometryId];
        rayContext.currentGeometry_ = &(*raytracerGeometries_)[raytracerGeometryId];

        // Initialize current geometry data
        const Vector3& faceNormal = geometryBuffer_->faceNormals_[elementIndex];
        position = geometryBuffer_->positions_[elementIndex];

        // Initialize per-element state
        currentSmoothNormal_ = geometryBuffer_->smoothNormals_[elementIndex];
        currentGeometryId_ = geometryId;

        accumulatedLight_ = Vector3::ZERO;

        return true;
    };

    /// Begin sample.
    void BeginSample(unsigned /*sampleIndex*/) {}

    /// End sample.
    void EndSample(const Vector3& light, const Vector3& direction)
    {
        const float intensity = ea::max(0.0f, currentSmoothNormal_.DotProduct(direction));
        accumulatedLight_ += light * intensity;
    }

    /// End tracing element.
    void EndElement(unsigned elementIndex)
    {
        const float weight = 1.0f / numSamples_;
        const Vector3 directLight = accumulatedLight_ * weight;

        if (bakeDirect_)
            bakedDirect_->directLight_[elementIndex] += directLight;

        if (bakeIndirect_)
        {
            const Vector3& albedo = geometryBuffer_->albedo_[elementIndex];
            bakedDirect_->surfaceLight_[elementIndex] += indirectBrightness_ * albedo * directLight;
        }
    }
};

/// Direct light tracing for light probes: tracing kernel.
struct LightProbeDirectTracingKernel
{
    /// Light probes collection.
    const LightProbeCollection* collection_{};
    /// Light probes baked data.
    LightProbeCollectionBakedData* bakedData_{};
    /// Settings.
    const DirectLightTracingSettings* settings_{};
    /// Raytracer geometries.
    const ea::vector<RaytracerGeometry>* raytracerGeometries_{};
    /// Number of samples.
    unsigned numSamples_{ 1 };
    /// Whether to bake direct light for direct lighting itself.
    bool bakeDirect_{};
    unsigned lightMask_{};

    /// Accumulated light SH.
    SphericalHarmonicsColor9 accumulatedLightSH_;

    /// Return number of elements to trace.
    unsigned GetNumElements() const { return bakedData_->Size(); }

    /// Return number of samples.
    unsigned GetNumSamples() const { return numSamples_; }

    /// Return geometry mask to use.
    unsigned GetGeometryMask() const { return RaytracerScene::PrimaryLODGeometry; }

    /// Return ray intersect context.
    DirectTracingContextForLightProbes GetRayContext()
    {
        DirectTracingContextForLightProbes rayContext;
        rtcInitIntersectContext(&rayContext);
        rayContext.geometryIndex_ = raytracerGeometries_;
        rayContext.filter = TracingFilterForLightProbesDirect;
        return rayContext;
    }

    /// Begin tracing element.
    bool BeginElement(unsigned elementIndex, DirectTracingContextForLightProbes& /*rayContext*/, Vector3& position)
    {
        const unsigned probeLightMask = collection_->lightMasks_[elementIndex];
        if ((probeLightMask & lightMask_) == 0)
            return false;

        position = collection_->worldPositions_[elementIndex];

        accumulatedLightSH_ = {};

        return true;
    };

    /// Begin sample. Return position, normal and initial ray direction.
    void BeginSample(unsigned /*sampleIndex*/)
    {
    }

    /// End sample.
    void EndSample(const Vector3& light, const Vector3& direction)
    {
        accumulatedLightSH_ += SphericalHarmonicsColor9(direction, light);
    }

    /// End tracing element.
    void EndElement(unsigned elementIndex)
    {
        if (bakeDirect_)
        {
            const float weight = M_PI / numSamples_;

            const SphericalHarmonicsDot9 sh{ accumulatedLightSH_ * weight };
            bakedData_->sphericalHarmonics_[elementIndex] += sh;
        }
    }
};

/// Trace direct lighting.
template <class T, class U>
void TraceDirectLight(T sharedKernel, U sharedGenerator,
    const RaytracerScene& raytracerScene, const DirectLightTracingSettings& settings)
{
    RTCScene scene = raytracerScene.GetEmbreeScene();
    const ea::vector<RaytracerGeometry>& raytracerGeometries = raytracerScene.GetGeometries();

    ParallelFor(sharedKernel.GetNumElements(), settings.numTasks_,
        [&](unsigned fromIndex, unsigned toIndex)
    {
        auto kernel = sharedKernel;
        auto generator = sharedGenerator;

        auto rayContext = sharedKernel.GetRayContext();

        Vector3 incomingLightIntensity;
        rayContext.incomingLight_ = &incomingLightIntensity;

        RTCRayHit rayHit;
        rayHit.ray.mask = sharedKernel.GetGeometryMask();
        rayHit.ray.tnear = 0.0f;
        rayHit.ray.time = 0.0f;
        rayHit.ray.id = 0;
        rayHit.ray.flags = 0;

        for (unsigned elementIndex = fromIndex; elementIndex < toIndex; ++elementIndex)
        {
            Vector3 position;
            if (!kernel.BeginElement(elementIndex, rayContext, position))
                continue;

            for (unsigned sampleIndex = 0; sampleIndex < kernel.GetNumSamples(); ++sampleIndex)
            {
                kernel.BeginSample(sampleIndex);

                Vector3 rayOffset;
                Vector3 incomingLightDirection;
                if (!generator.Generate(position, rayOffset, incomingLightIntensity, incomingLightDirection))
                    continue;

                // Cast direct ray
                rayHit.ray.dir_x = rayOffset.x_;
                rayHit.ray.dir_y = rayOffset.y_;
                rayHit.ray.dir_z = rayOffset.z_;
                rayHit.ray.org_x = position.x_ - rayOffset.x_;
                rayHit.ray.org_y = position.y_ - rayOffset.y_;
                rayHit.ray.org_z = position.z_ - rayOffset.z_;
                rayHit.ray.tfar = 1.0f;
                rayHit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
                rtcIntersect1(scene, &rayContext, &rayHit);

                if (rayHit.hit.geomID == RTC_INVALID_GEOMETRY_ID)
                    kernel.EndSample(incomingLightIntensity, incomingLightDirection);
            }

            kernel.EndElement(elementIndex);
        }
    });
}

/// Ray tracing context for indirect light baking.
struct IndirectTracingContext : public RTCIntersectContext
{
    /// Geometry index.
    const ea::vector<RaytracerGeometry>* geometryIndex_{};
};

/// Filter function for indirect light baking.
void TracingFilterIndirect(const RTCFilterFunctionNArguments* args)
{
    const auto& ctx = *static_cast<const IndirectTracingContext*>(args->context);
    auto& hit = *reinterpret_cast<RTCHit*>(args->hit);
    assert(args->N == 1);

    // Ignore invalid
    if (args->valid[0] == 0)
        return;

    // Ignore if transparent
    const RaytracerGeometry& hitGeometry = (*ctx.geometryIndex_)[hit.geomID];
    if (IsTransparentForIndirect(hitGeometry, hit))
        args->valid[0] = 0;
}

/// Indirect light tracing for charts: tracing kernel.
struct ChartIndirectTracingKernel
{
    /// Indirect light chart.
    LightmapChartBakedIndirect* bakedIndirect_{};
    /// Geometry buffer.
    const LightmapChartGeometryBuffer* geometryBuffer_{};
    /// Light probes mesh for fallback.
    const TetrahedralMesh* lightProbesMesh_{};
    /// Light probes data for fallback.
    const LightProbeCollectionBakedData* lightProbesData_{};
    /// Mapping from geometry buffer ID to embree geometry ID.
    const ea::vector<unsigned>* geometryBufferToRaytracer_{};
    /// Raytracer geometries.
    const ea::vector<RaytracerGeometry>* raytracerGeometries_{};
    /// Settings.
    const IndirectLightTracingSettings* settings_{};

    /// Current position.
    Vector3 currentPosition_;
    /// Current normal of actual geometry face.
    Vector3 currentFaceNormal_;
    /// Current smooth interpolated normal.
    Vector3 currentSmoothNormal_;
    /// Current geometry ID.
    unsigned currentGeometryId_;
    unsigned currentBackgroundIndex_{};

    /// Accumulated indirect light value.
    Vector4 accumulatedIndirectLight_;

    /// Last sampled tetrahedron.
    unsigned lightProbesMeshHint_{};

    /// Return number of elements to trace.
    unsigned GetNumElements() const { return bakedIndirect_->light_.size(); }

    /// Return number of samples.
    unsigned GetNumSamples() const { return settings_->maxSamples_; }

    /// Begin tracing element.
    bool BeginElement(unsigned elementIndex)
    {
        const unsigned geometryId = geometryBuffer_->geometryIds_[elementIndex];
        if (!geometryId)
            return false;

        currentPosition_ = geometryBuffer_->positions_[elementIndex];
        currentFaceNormal_ = geometryBuffer_->faceNormals_[elementIndex];
        currentSmoothNormal_ = geometryBuffer_->smoothNormals_[elementIndex];
        currentGeometryId_ = geometryId;
        currentBackgroundIndex_ = geometryBuffer_->backgroundIds_[elementIndex];

        // Fallback to light probes if has LODs
        const unsigned raytracerGeometryId = (*geometryBufferToRaytracer_)[geometryId];
        const RaytracerGeometry& raytracerGeometry = (*raytracerGeometries_)[raytracerGeometryId];

        if (raytracerGeometry.numLods_ > 1)
        {
            const SphericalHarmonicsDot9 sh = lightProbesMesh_->Sample(
                lightProbesData_->sphericalHarmonics_, currentPosition_, lightProbesMeshHint_);
            const Vector3 indirectLightValue = VectorMax(Vector3::ZERO, sh.Evaluate(currentSmoothNormal_));
            bakedIndirect_->light_[elementIndex] += { indirectLightValue, 1.0f };
            return false;
        }

        accumulatedIndirectLight_ = Vector4::ZERO;

        return true;
    };

    unsigned GetElementBackgroundIndex() const { return currentBackgroundIndex_; }

    /// Begin sample. Return position, normal and initial ray direction.
    void BeginSample(unsigned /*sampleIndex*/,
        Vector3& position, Vector3& faceNormal, Vector3& smoothNormal, Vector3& rayDirection, Vector3& albedo) const
    {
        position = currentPosition_;
        faceNormal = currentFaceNormal_;
        smoothNormal = currentSmoothNormal_;
        albedo = Vector3::ONE;
        rayDirection = RandomHemisphereDirection(currentFaceNormal_);
    }

    /// End sample.
    void EndSample(const Vector3& light)
    {
        accumulatedIndirectLight_ += Vector4(light, 1.0f);
    }

    /// End tracing element.
    void EndElement(unsigned elementIndex)
    {
        bakedIndirect_->light_[elementIndex] += accumulatedIndirectLight_;
    }
};

/// Indirect light tracing for light probes: tracing kernel.
struct LightProbeIndirectTracingKernel
{
    /// Light probes collection.
    const LightProbeCollection* collection_{};
    /// Light probes baked data.
    LightProbeCollectionBakedData* bakedData_{};
    /// Settings.
    const IndirectLightTracingSettings* settings_{};

    /// Current position.
    Vector3 currentPosition_;

    /// Current sample direction.
    Vector3 currentSampleDirection_;

    /// Accumulated indirect light (SH).
    SphericalHarmonicsColor9 accumulatedLightSH_;

    /// Return number of elements to trace.
    unsigned GetNumElements() const { return bakedData_->Size(); }

    /// Return number of samples.
    unsigned GetNumSamples() const { return settings_->maxSamples_; }

    /// Begin tracing element.
    bool BeginElement(unsigned elementIndex)
    {
        currentPosition_ = collection_->worldPositions_[elementIndex];

        accumulatedLightSH_ = {};
        return true;
    };

    unsigned GetElementBackgroundIndex() const { return 0; }

    /// Begin sample. Return position, normal and initial ray direction.
    void BeginSample(unsigned /*sampleIndex*/,
        Vector3& position, Vector3& faceNormal, Vector3& smoothNormal, Vector3& rayDirection, Vector3& albedo)
    {
        RandomDirection3(currentSampleDirection_);

        position = currentPosition_;
        faceNormal = currentSampleDirection_;
        smoothNormal = currentSampleDirection_;
        rayDirection = currentSampleDirection_;
        albedo = Vector3::ONE;
    }

    /// End sample.
    void EndSample(const Vector3& light)
    {
        accumulatedLightSH_ += SphericalHarmonicsColor9(currentSampleDirection_, light);
    }

    /// End tracing element.
    void EndElement(unsigned elementIndex)
    {
        const float weight = M_PI / GetNumSamples();
        const SphericalHarmonicsDot9 sh{ accumulatedLightSH_ * weight };
        bakedData_->sphericalHarmonics_[elementIndex] += sh;
    }
};

/// Trace indirect lighting.
template <class T>
void TraceIndirectLight(T sharedKernel, const ea::vector<const LightmapChartBakedDirect*>& bakedDirect,
    const RaytracerScene& raytracerScene, const IndirectLightTracingSettings& settings)
{
    assert(settings.maxBounces_ <= IndirectLightTracingSettings::MaxBounces);

    ParallelFor(sharedKernel.GetNumElements(), settings.numTasks_,
        [&](unsigned fromIndex, unsigned toIndex)
    {
        T kernel = sharedKernel;

        RTCScene scene = raytracerScene.GetEmbreeScene();
        const float maxDistance = raytracerScene.GetMaxDistance();
        const auto& geometryIndex = raytracerScene.GetGeometries();
        const auto& backgrounds = raytracerScene.GetBackgrounds();

        Vector3 albedo[IndirectLightTracingSettings::MaxBounces];
        Vector3 incomingSamples[IndirectLightTracingSettings::MaxBounces];
        float incomingFactors[IndirectLightTracingSettings::MaxBounces];

        RTCRayHit rayHit;
        IndirectTracingContext rayContext;
        rtcInitIntersectContext(&rayContext);
        rayContext.geometryIndex_ = &geometryIndex;
        rayContext.filter = TracingFilterIndirect;

        rayHit.ray.tnear = 0.0f;
        rayHit.ray.time = 0.0f;
        rayHit.ray.id = 0;
        rayHit.ray.mask = RaytracerScene::PrimaryLODGeometry;
        rayHit.ray.flags = 0;

        for (unsigned elementIndex = fromIndex; elementIndex < toIndex; ++elementIndex)
        {
            if (!kernel.BeginElement(elementIndex))
                continue;

            const BakedSceneBackground& background = (*backgrounds)[kernel.GetElementBackgroundIndex()];

            for (unsigned sampleIndex = 0; sampleIndex < kernel.GetNumSamples(); ++sampleIndex)
            {
                Vector3 currentPosition;
                Vector3 currentFaceNormal;
                Vector3 currentSmoothNormal;
                Vector3 currentRayDirection;
                kernel.BeginSample(sampleIndex,
                    currentPosition, currentFaceNormal, currentSmoothNormal, currentRayDirection, albedo[0]);

                int numBounces = 0;
                for (unsigned bounceIndex = 0; bounceIndex < settings.maxBounces_; ++bounceIndex)
                {
                    rayHit.ray.org_x = currentPosition.x_;
                    rayHit.ray.org_y = currentPosition.y_;
                    rayHit.ray.org_z = currentPosition.z_;
                    rayHit.ray.dir_x = currentRayDirection.x_;
                    rayHit.ray.dir_y = currentRayDirection.y_;
                    rayHit.ray.dir_z = currentRayDirection.z_;
                    rayHit.ray.tfar = maxDistance;
                    rayHit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
                    rtcIntersect1(scene, &rayContext, &rayHit);

                    // If hit background, pick light and break
                    if (rayHit.hit.geomID == RTC_INVALID_GEOMETRY_ID)
                    {
                        incomingSamples[bounceIndex] = background.SampleLinear(currentRayDirection);
                        incomingFactors[bounceIndex] = 1.0f;
                        ++numBounces;
                        break;
                    }

                    // Check normal orientation
                    if (currentRayDirection.DotProduct({ rayHit.hit.Ng_x, rayHit.hit.Ng_y, rayHit.hit.Ng_z }) > 0.0f)
                        break;

                    // Sample lightmap UV
                    const RaytracerGeometry& geometry = geometryIndex[rayHit.hit.geomID];
                    Vector2 lightmapUV;
                    rtcInterpolate0(geometry.embreeGeometry_, rayHit.hit.primID, rayHit.hit.u, rayHit.hit.v,
                        RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, RaytracerScene::LightmapUVAttribute, &lightmapUV.x_, 2);

                    // Modify incoming flux
                    const float probability = 1 / (2 * M_PI);
                    const float cosTheta = ea::max(0.0f, currentRayDirection.DotProduct(currentSmoothNormal));
                    const float reflectance = 1 / M_PI;
                    const float brdf = reflectance / M_PI;

                    const unsigned lightmapIndex = geometry.lightmapIndex_;
                    const IntVector2 sampleLocation = bakedDirect[lightmapIndex]->GetNearestLocation(lightmapUV);
                    incomingSamples[bounceIndex] = bakedDirect[lightmapIndex]->GetSurfaceLight(sampleLocation);
                    incomingFactors[bounceIndex] = brdf * cosTheta / probability;
                    ++numBounces;

                    // Go to next hemisphere
                    if (numBounces < settings.maxBounces_)
                    {
                        // Update albedo for hit surface
                        albedo[bounceIndex + 1] = bakedDirect[lightmapIndex]->GetAlbedo(sampleLocation);

                        // Move to hit position
                        currentPosition.x_ = rayHit.ray.org_x + rayHit.ray.dir_x * rayHit.ray.tfar;
                        currentPosition.y_ = rayHit.ray.org_y + rayHit.ray.dir_y * rayHit.ray.tfar;
                        currentPosition.z_ = rayHit.ray.org_z + rayHit.ray.dir_z * rayHit.ray.tfar;

                        // Offset position a bit
                        const Vector3 hitNormal = Vector3(rayHit.hit.Ng_x, rayHit.hit.Ng_y, rayHit.hit.Ng_z).Normalized();
                        const float bias = settings.scaledPositionBounceBias_ * CalculateBiasScale(currentPosition);
                        currentPosition.x_ += Sign(hitNormal.x_) * bias + hitNormal.x_ * settings.constPositionBounceBias_;
                        currentPosition.y_ += Sign(hitNormal.y_) * bias + hitNormal.y_ * settings.constPositionBounceBias_;
                        currentPosition.z_ += Sign(hitNormal.z_) * bias + hitNormal.z_ * settings.constPositionBounceBias_;

                        // Update smooth normal
                        rtcInterpolate0(geometry.embreeGeometry_, rayHit.hit.primID, rayHit.hit.u, rayHit.hit.v,
                            RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, RaytracerScene::NormalAttribute, &currentSmoothNormal.x_, 3);
                        currentSmoothNormal = currentSmoothNormal.Normalized();

                        // Update face normal and find new direction to sample
                        currentFaceNormal = hitNormal;
                        currentRayDirection = RandomHemisphereDirection(currentFaceNormal);
                    }
                }

                // Accumulate samples back-to-front
                Vector3 sampleIndirectLight;
                for (int bounceIndex = numBounces - 1; bounceIndex >= 0; --bounceIndex)
                {
                    sampleIndirectLight += incomingSamples[bounceIndex];
                    sampleIndirectLight *= incomingFactors[bounceIndex];
                    sampleIndirectLight *= albedo[bounceIndex];
                }

                kernel.EndSample(sampleIndirectLight);
            }
            kernel.EndElement(elementIndex);
        }
    });
}

}

void PreprocessGeometryBuffer(LightmapChartGeometryBuffer& geometryBuffer,
    const RaytracerScene& raytracerScene, const ea::vector<unsigned>& geometryBufferToRaytracer,
    const GeometryBufferPreprocessSettings& settings)
{
    RTCScene scene = raytracerScene.GetEmbreeScene();
    const ea::vector<RaytracerGeometry>& raytracerGeometries = raytracerScene.GetGeometries();
    ParallelFor(geometryBuffer.positions_.size(), settings.numTasks_,
        [&](unsigned fromIndex, unsigned toIndex)
    {
        RTCRayHit rayHit;
        GeometryBufferPreprocessContext rayContext;
        rayContext.geometryIndex_ = &raytracerGeometries;
        rtcInitIntersectContext(&rayContext);
        rayContext.filter = GeometryBufferPreprocessFilter;

        rayHit.ray.mask = RaytracerScene::AllGeometry;
        rayHit.ray.tnear = 0.0f;
        rayHit.ray.time = 0.0f;
        rayHit.ray.id = 0;
        rayHit.ray.flags = 0;

        for (unsigned i = fromIndex; i < toIndex; ++i)
        {
            const unsigned geometryId = geometryBuffer.geometryIds_[i];
            if (!geometryId)
                continue;

            rayContext.currentGeometry_ = &raytracerGeometries[geometryBufferToRaytracer[geometryId]];

            Vector3& mutablePosition = geometryBuffer.positions_[i];
            const float biasScale = CalculateBiasScale(mutablePosition);
            const float bias = biasScale * settings.scaledPositionBackfaceBias_ + settings.constPositionBackfaceBias_;

            const Vector3 faceNormal = geometryBuffer.faceNormals_[i];
            const float texelRadius = geometryBuffer.texelRadiuses_[i];
            const Quaternion basis{ Vector3::FORWARD, faceNormal };

            static const unsigned NumSamples = 4;
            static const Vector3 sampleRays[NumSamples] = { Vector3::LEFT, Vector3::RIGHT, Vector3::UP, Vector3::DOWN };

            // Find closest backface hit
            float closestHitDistance = M_LARGE_VALUE;
            Vector3 closestHitDirection;

            for (unsigned i = 0; i < NumSamples; ++i)
            {
                const Vector3 rayDirection = basis * sampleRays[i];

                // Push position in the opposite direction just a bit
                rayHit.ray.org_x = mutablePosition.x_ - rayDirection.x_ * bias;
                rayHit.ray.org_y = mutablePosition.y_ - rayDirection.y_ * bias;
                rayHit.ray.org_z = mutablePosition.z_ - rayDirection.z_ * bias;

                rayHit.ray.dir_x = rayDirection.x_ * (1.0f + bias);
                rayHit.ray.dir_y = rayDirection.y_ * (1.0f + bias);
                rayHit.ray.dir_z = rayDirection.z_ * (1.0f + bias);
                rayHit.ray.tfar = texelRadius;
                rayHit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
                rtcIntersect1(scene, &rayContext, &rayHit);

                if (rayHit.hit.geomID == RTC_INVALID_GEOMETRY_ID)
                    continue;

                // Frontface if dot product is negative, i.e face and ray face each other
                const float dp = rayHit.hit.Ng_x * rayHit.ray.dir_x
                    + rayHit.hit.Ng_y * rayHit.ray.dir_y
                    + rayHit.hit.Ng_z * rayHit.ray.dir_z;

                // Normal is not normalized, so epsilon won't really help
                if (dp < 0.0f)
                    continue;

                // Find closest hit
                if (rayHit.ray.tfar < closestHitDistance)
                {
                    closestHitDistance = rayHit.ray.tfar;
                    closestHitDirection = rayDirection;
                }
            }

            // Push the position behind closest hit
            const float offset = closestHitDistance + settings.constPositionBackfaceBias_
                + settings.scaledPositionBackfaceBias_ * CalculateBiasScale(mutablePosition);

            if (closestHitDistance != M_LARGE_VALUE)
            {
                mutablePosition = { rayHit.ray.org_x, rayHit.ray.org_y, rayHit.ray.org_z };
                mutablePosition += closestHitDirection * offset;
            }
        }
    });
}

void BakeEmissionLight(LightmapChartBakedDirect& bakedDirect, const LightmapChartGeometryBuffer& geometryBuffer,
    const EmissionLightTracingSettings& settings, float indirectBrightnessMultiplier)
{
    ParallelFor(bakedDirect.directLight_.size(), settings.numTasks_,
        [&](unsigned fromIndex, unsigned toIndex)
    {
        for (unsigned i = fromIndex; i < toIndex; ++i)
        {
            const unsigned geometryId = geometryBuffer.geometryIds_[i];
            if (!geometryId)
                continue;

            const Vector3& albedo = geometryBuffer.albedo_[i];
            const Vector3& emission = geometryBuffer.emission_[i];

            bakedDirect.directLight_[i] += emission;
            bakedDirect.surfaceLight_[i] += indirectBrightnessMultiplier * emission;
            bakedDirect.albedo_[i] = albedo;
        }
    });
}

void BakeDirectLightForCharts(LightmapChartBakedDirect& bakedDirect, const LightmapChartGeometryBuffer& geometryBuffer,
    const RaytracerScene& raytracerScene, const ea::vector<unsigned>& geometryBufferToRaytracer,
    const BakedLight& light, const DirectLightTracingSettings& settings)
{
    const bool bakeDirect = light.lightMode_ == LM_BAKED;
    const bool bakeIndirect = true;
    const unsigned numSamples = CalculateNumSamples(light, settings.maxSamples_);
    const ChartDirectTracingKernel kernel{ &bakedDirect, &geometryBuffer, &geometryBufferToRaytracer,
        &raytracerScene.GetGeometries(), &settings, light.indirectBrightness_, numSamples,
        bakeDirect, bakeIndirect, light.lightMask_ };

    if (light.lightType_ == LIGHT_DIRECTIONAL)
    {
        const RayGeneratorForDirectLight generator{ light.color_, light.direction_, light.rotation_,
            raytracerScene.GetMaxDistance(), light.halfAngleTan_ };
        TraceDirectLight(kernel, generator, raytracerScene, settings);
    }
    else if (light.lightType_ == LIGHT_POINT)
    {
        const RayGeneratorForPointLight generator{ light.color_, light.position_, light.distance_, light.radius_ };
        TraceDirectLight(kernel, generator, raytracerScene, settings);
    }
    else if (light.lightType_ == LIGHT_SPOT)
    {
        const RayGeneratorForSpotLight generator{ light.color_, light.position_, light.direction_, light.rotation_,
            light.distance_, light.radius_, light.cutoff_ };
        TraceDirectLight(kernel, generator, raytracerScene, settings);
    }
}

void BakeDirectLightForLightProbes(
    LightProbeCollectionBakedData& bakedData, const LightProbeCollection& collection,
    const RaytracerScene& raytracerScene, const BakedLight& light, const DirectLightTracingSettings& settings)
{
    const bool bakeDirect = light.lightMode_ == LM_BAKED;
    const unsigned numSamples = CalculateNumSamples(light, settings.maxSamples_);
    const LightProbeDirectTracingKernel kernel{ &collection, &bakedData, &settings,
        &raytracerScene.GetGeometries(), numSamples, bakeDirect, light.lightMask_ };

    if (light.lightType_ == LIGHT_DIRECTIONAL)
    {
        const RayGeneratorForDirectLight generator{ light.color_, light.direction_, light.rotation_,
            raytracerScene.GetMaxDistance(), light.halfAngleTan_ };
        TraceDirectLight(kernel, generator, raytracerScene, settings);
    }
    else if (light.lightType_ == LIGHT_POINT)
    {
        const RayGeneratorForPointLight generator{ light.color_, light.position_, light.distance_, light.radius_ };
        TraceDirectLight(kernel, generator, raytracerScene, settings);
    }
    else if (light.lightType_ == LIGHT_SPOT)
    {
        const RayGeneratorForSpotLight generator{ light.color_, light.position_, light.direction_, light.rotation_,
            light.distance_, light.radius_, light.cutoff_ };
        TraceDirectLight(kernel, generator, raytracerScene, settings);
    }
}

void BakeIndirectLightForCharts(LightmapChartBakedIndirect& bakedIndirect,
    const ea::vector<const LightmapChartBakedDirect*>& bakedDirect, const LightmapChartGeometryBuffer& geometryBuffer,
    const TetrahedralMesh& lightProbesMesh, const LightProbeCollectionBakedData& lightProbesData,
    const RaytracerScene& raytracerScene, const ea::vector<unsigned>& geometryBufferToRaytracer,
    const IndirectLightTracingSettings& settings)
{
    if (settings.maxBounces_ == 0)
        return;

    const ChartIndirectTracingKernel kernel{ &bakedIndirect, &geometryBuffer, &lightProbesMesh, &lightProbesData,
        &geometryBufferToRaytracer, &raytracerScene.GetGeometries(), &settings };
    TraceIndirectLight(kernel, bakedDirect, raytracerScene, settings);
}

void BakeIndirectLightForLightProbes(
    LightProbeCollectionBakedData& bakedData, const LightProbeCollection& collection,
    const ea::vector<const LightmapChartBakedDirect*>& bakedDirect,
    const RaytracerScene& raytracerScene, const IndirectLightTracingSettings& settings)
{
    if (settings.maxBounces_ == 0)
        return;

    const LightProbeIndirectTracingKernel kernel{ &collection, &bakedData, &settings };
    TraceIndirectLight(kernel, bakedDirect, raytracerScene, settings);
}

}
