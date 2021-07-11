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

#pragma once

#include "../Core/NonCopyable.h"
#include "../Graphics/Light.h"
#include "../RenderPipeline/CommonTypes.h"
#include "../RenderPipeline/ShadowSplitProcessor.h"

#include <EASTL/array.h>
#include <EASTL/vector.h>

namespace Urho3D
{

class Camera;
class DrawableProcessor;
class LightProcessor;

/// Light processor callback.
class LightProcessorCallback
{
public:
    /// Return whether light needs shadow.
    virtual bool IsLightShadowed(Light* light) = 0;
    /// Allocate shadow map for one frame.
    virtual ShadowMapRegion AllocateTransientShadowMap(const IntVector2& size) = 0;
};

struct CookedLightParams
{
    /// Common light parameters
    /// @{
    Vector3 direction_;
    Vector3 position_;
    float inverseRange_{};
    Vector3 effectiveColorInGammaSpace_;
    Vector3 effectiveColorInLinearSpace_;
    float effectiveSpecularIntensity_{};

    float spotCutoff_{};
    float inverseSpotCutoff_{};

    float volumetricRadius_{};
    float volumetricLength_{};
    /// @}

    /// Shadow matrices for each split (for directional lights).
    /// Light matrix and shadow matrix (for spot lights).
    /// Light matrix (for point lights).
    ea::array<Matrix4, MAX_CASCADE_SPLITS> lightMatrices_;
    unsigned numLightMatrices_{};

    /// Shadow mapping parameters
    /// @{
    Vector4 shadowCubeAdjust_;
    Vector4 shadowDepthFade_;
    Vector4 shadowIntensity_;
    Vector2 shadowMapInvSize_;
    Vector2 shadowCubeUVBias_;
    Vector4 shadowSplitDistances_;
    /// @}

    ea::array<float, MAX_LIGHT_SPLITS> shadowNormalBias_;
    ea::array<float, MAX_LIGHT_SPLITS> shadowDepthBiasMultiplier_{};

    Texture2D* shadowMap_{};
    Texture* lightRamp_{};
    Texture* lightShape_{};

    /// Return light color in appropriate color space.
    Vector3 GetColor(bool isLinear) const { return isLinear ? effectiveColorInLinearSpace_ : effectiveColorInGammaSpace_; }
};

/// Manages light parameters, lit geometries, shadow splits and shadow casters.
class URHO3D_API LightProcessor : public NonCopyable
{
public:
    /// Number of frames for shadow splits expiration.
    static const unsigned NumSplitFramesToLive = 600;

    explicit LightProcessor(Light* light);
    ~LightProcessor();

    /// Begin update from main thread.
    void BeginUpdate(DrawableProcessor* drawableProcessor, LightProcessorCallback* callback);
    /// Update light in worker thread.
    void Update(DrawableProcessor* drawableProcessor);
    /// End update from main thread.
    void EndUpdate(DrawableProcessor* drawableProcessor, LightProcessorCallback* callback);

    /// Return pipeline state hashes
    /// @{
    unsigned GetForwardLitHash() const { return forwardLitBatchHash_; }
    unsigned GetShadowHash(unsigned splitIndex) const { return shadowBatchStateHashes_[splitIndex]; }
    unsigned GetLightVolumeHash() const { return lightVolumeBatchHash_; }
    /// @}

    /// Return values are always valid
    /// @{
    Light* GetLight() const { return light_; }
    /// @}

    /// Return values are valid after threaded update
    /// @{
    const ea::vector<Drawable*>& GetLitGeometries() const { return litGeometries_; }
    bool HasForwardLitGeometries() const { return hasForwardLitGeometries_; }
    bool HasLitGeometries() const { return hasLitGeometries_; }

    bool DoesOverlapCamera() const { return cameraIsInsideLightVolume_; }
    bool HasShadow() const { return numActiveSplits_ != 0; }
    IntVector2 GetShadowMapSize() const { return numActiveSplits_ != 0 ? shadowMapSize_ : IntVector2::ZERO; }
    unsigned GetNumSplits() const { return numActiveSplits_; }
    /// @}

    /// Return values are valid after update is finished
    /// @{
    const ShadowSplitProcessor* GetSplit(unsigned splitIndex) const { return &splits_[splitIndex]; }
    ShadowSplitProcessor* GetMutableSplit(unsigned splitIndex) { return &splits_[splitIndex]; }
    ea::span<const ShadowSplitProcessor> GetSplits() const { return { splits_.data(), numActiveSplits_ }; }

    ShadowMapRegion GetShadowMap() const { return shadowMap_; }
    const CookedLightParams& GetParams() const { return cookedParams_; }
    /// @}

private:
    void InitializeShadowSplits(DrawableProcessor* drawableProcessor);
    void UpdateHashes();
    void CookShaderParameters(Camera* cullCamera, float subPixelOffset);
    IntVector2 GetNumSplitsInGrid() const;

    Light* light_{};
    ea::vector<ShadowSplitProcessor> splits_;
    unsigned splitRemainingTimeToLive_{};

    /// Parameters extracted from light settings
    /// @{
    bool isShadowRequested_{};
    unsigned numSplitsRequested_{};
    /// @}

    /// Processing results
    /// @{
    bool cameraIsInsideLightVolume_{};
    unsigned numActiveSplits_{};
    int shadowMapSplitSize_{};
    IntVector2 shadowMapSize_{};
    bool hasLitGeometries_{};
    bool hasForwardLitGeometries_{};
    /// Point and spot lights: only forward lit geometries.
    /// Directional lights: all lit geometries, for shadow focusing.
    ea::vector<Drawable*> litGeometries_;
    /// Point and spot lights: all possible shadow casters.
    /// Directional lights: temporary buffer for split queries.
    ea::vector<Drawable*> shadowCasterCandidates_;
    /// Accumulative shadow map region containing all the splits.
    ShadowMapRegion shadowMap_;
    CookedLightParams cookedParams_;
    /// @}

    /// Pipeline state hashes
    /// @{
    unsigned forwardLitBatchHash_{};
    unsigned lightVolumeBatchHash_{};
    ea::array<unsigned, MAX_LIGHT_SPLITS> shadowBatchStateHashes_{};
    /// @}
};

/// Cache of light processors.
// TODO(renderer): Add automatic expiration by time, add cache cleanup
class URHO3D_API LightProcessorCache : public NonCopyable
{
public:
    LightProcessorCache();
    ~LightProcessorCache();
    LightProcessor* GetLightProcessor(Light* light);

private:
    ea::unordered_map<WeakPtr<Light>, ea::unique_ptr<LightProcessor>> cache_;
};

}
