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

#include "../Graphics/Camera.h"
#include "../Math/NumericRange.h"
#include "../RenderPipeline/RenderPipelineDefs.h"
#include "../RenderPipeline/PipelineBatchSortKey.h"
#include "../Scene/Node.h"

#include <EASTL/vector.h>

namespace Urho3D
{

class Camera;
class Drawable;
class DrawableProcessor;
class Light;
class LightProcessor;

/// Manages single shadow split parameters and shadow casters.
/// Spot lights always have one split.
/// Directions lights have one split per cascade.
/// Point lights always have six splits.
class URHO3D_API ShadowSplitProcessor
{
public:
    ShadowSplitProcessor(LightProcessor* owner, unsigned splitIndex);
    ~ShadowSplitProcessor();

    /// Initialize split parameters
    /// @{
    void InitializeDirectional(DrawableProcessor* drawableProcessor,
        const FloatRange& splitRange, const ea::vector<Drawable*>& litGeometries);
    void InitializeSpot();
    void InitializePoint(CubeMapFace face);
    /// @}

    /// Process shadow casters
    /// @{
    void ProcessDirectionalShadowCasters(DrawableProcessor* drawableProcessor, ea::vector<Drawable*>& shadowCastersBuffer);
    void ProcessSpotShadowCasters(DrawableProcessor* drawableProcessor, const ea::vector<Drawable*>& shadowCasterCandidates);
    void ProcessPointShadowCasters(DrawableProcessor* drawableProcessor, const ea::vector<Drawable*>& shadowCasterCandidates);
    /// @}

    void FinalizeShadow(const ShadowMapRegion& shadowMap, unsigned pcfKernelSize);
    void FinalizeShadowBatches();

    /// Return immutable
    /// @{
    LightProcessor* GetLightProcessor() const { return lightProcessor_; }
    Light* GetLight() const { return light_; }
    unsigned GetSplitIndex() const { return splitIndex_; }
    /// @}

    /// Return values are valid after shadow casters are processed
    /// @{
    const auto& GetShadowCasters() const { return shadowCasters_; }
    bool HasShadowCasters() const { return !shadowCasters_.empty(); }
    /// @}

    /// Return values are valid after shadow map is finalized
    /// @{
    Matrix4 GetWorldToShadowSpaceMatrix(float subPixelOffset) const;
    const ShadowMapRegion& GetShadowMap() const { return shadowMap_; }
    float GetShadowMapTexelSizeInWorldSpace() const { return shadowMapWorldSpaceTexelSize_; }
    const FloatRange& GetCascadeZRange() const { return cascadeZRange_; }
    Camera* GetShadowCamera() const { return shadowCamera_; }
    /// @}

    auto& GetMutableUnsortedShadowBatches() { return unsortedShadowBatches_; }
    auto& GetMutableShadowBatches() { return shadowBatches_; }
    const auto& GetShadowBatches() const { return shadowBatches_; }

private:
    void InitializeBaseDirectionalCamera(Camera* cullCamera);
    BoundingBox GetLitGeometriesBoundingBox(
        DrawableProcessor* drawableProcessor, const ea::vector<Drawable*>& litGeometries) const;
    BoundingBox GetSplitShadowBoundingBoxInLightSpace(
        DrawableProcessor* drawableProcessor, const ea::vector<Drawable*>& litGeometries) const;
    void AdjustDirectionalLightCamera(const BoundingBox& lightSpaceBoundingBox, float shadowMapSize);

    /// Immutable
    /// @{
    LightProcessor* lightProcessor_{};
    Light* light_{};
    unsigned splitIndex_{};
    /// @}

    /// Internal cached objects
    /// @{
    SharedPtr<Node> shadowCameraNode_;
    SharedPtr<Camera> shadowCamera_;
    /// @}

    /// Frame-specific objects
    /// @{
    FloatRange cascadeZRange_{};
    FloatRange focusedCascadeZRange_{};
    ea::vector<Drawable*> shadowCasters_;

    ShadowMapRegion shadowMap_;
    float shadowMapWorldSpaceTexelSize_{};
    /// @}

    /// Shadow casters
    /// @{
    ea::vector<PipelineBatch> unsortedShadowBatches_;
    ea::vector<PipelineBatchByState> sortedShadowBatches_;
    PipelineBatchGroup<PipelineBatchByState> shadowBatches_;
    /// @}
};

}
