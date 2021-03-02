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
#include "../RenderPipeline/PipelineBatchSortKey.h"
#include "../RenderPipeline/ShadowMapAllocator.h"

#include <EASTL/vector.h>

namespace Urho3D
{

class Camera;
class Drawable;
class DrawableProcessor;
class Light;
class LightProcessor;
class Node;

/// Class that manages single shadow split processing.
class URHO3D_API ShadowSplitProcessor
{
public:
    /// Construct.
    ShadowSplitProcessor(LightProcessor* owner, unsigned splitIndex);
    /// Destruct.
    ~ShadowSplitProcessor();

    /// Initialize split for direction light.
    void InitializeDirectional(DrawableProcessor* drawableProcessor,
        const FloatRange& splitRange, const ea::vector<Drawable*>& litGeometries);
    /// Initialize split for spot light.
    void InitializeSpot();
    /// Initialize split for point light.
    void InitializePoint(CubeMapFace face);
    /// Process shadow casters for directional light split.
    void ProcessDirectionalShadowCasters(DrawableProcessor* drawableProcessor, ea::vector<Drawable*>& shadowCastersBuffer);
    /// Process shadow casters for spot light split.
    void ProcessSpotShadowCasters(DrawableProcessor* drawableProcessor, const ea::vector<Drawable*>& shadowCasterCandidates);
    /// Process shadow casters for point light split.
    void ProcessPointShadowCasters(DrawableProcessor* drawableProcessor, const ea::vector<Drawable*>& shadowCasterCandidates);
    /// Finalize shadow.
    void Finalize(const ShadowMap& shadowMap);

    /// Calculate shadow matrix.
    Matrix4 CalculateShadowMatrix(float subPixelOffset) const;

    /// Return light processor.
    LightProcessor* GetLightProcessor() const { return owner_; }
    /// Return light.
    Light* GetLight() const { return light_; }
    unsigned GetSplitIndex() const { return splitIndex_; }
    /// Return shadow map.
    const ShadowMap& GetShadowMap() const { return shadowMap_; }
    float GetShadowMapTexelSizeInWorldSpace() const { return shadowMapWorldSpaceTexelSize_; }
    /// Return shadow camera.
    Camera* GetShadowCamera() const { return shadowCamera_; }
    /// Return split Z range.
    const FloatRange& GetSplitZRange() const { return splitRange_; }
    /// Return shadow casters.
    const auto& GetShadowCasters() const { return shadowCasters_; }
    /// Return mutable shadow batches.
    auto& GetMutableShadowBatches() { return shadowCasterBatches_; }
    /// Return whether the split has shadow casters.
    bool HasShadowCasters() const { return !shadowCasters_.empty(); }

    /// Sort and return shadow batches. Array is cleared automatically.
    void SortShadowBatches(ea::vector<PipelineBatchByState>& sortedBatches) const;

private:
    /// Initialize base directional camera w/o any focusing and adjusting.
    void InitializeBaseDirectionalCamera(Camera* cullCamera);
    /// Return bounding box of lit geometries in split.
    BoundingBox GetLitBoundingBox(DrawableProcessor* drawableProcessor, const ea::vector<Drawable*>& litGeometries) const;
    /// Return split bounding box in light space.
    BoundingBox GetSplitBoundingBox(DrawableProcessor* drawableProcessor, const ea::vector<Drawable*>& litGeometries) const;
    /// Adjust directional camera to keep texels stable.
    void AdjustDirectionalCamera(const BoundingBox& lightSpaceBoundingBox, float shadowMapSize);

    /// Owner light processor.
    LightProcessor* owner_{};
    unsigned splitIndex_{};
    /// Light.
    Light* light_{};

    /// Shadow camera node.
    SharedPtr<Node> shadowCameraNode_;
    /// Shadow camera.
    SharedPtr<Camera> shadowCamera_;

    /// Split Z range (for directional lights only).
    FloatRange splitRange_{};
    /// Split Z range clipped to scene Z range (for focused directional lights only).
    FloatRange focusedSplitRange_{};

    /// Shadow casters.
    ea::vector<Drawable*> shadowCasters_;
    /// Shadow caster batches.
    ea::vector<PipelineBatch> shadowCasterBatches_;

    /// Shadow map for split.
    ShadowMap shadowMap_;
    float shadowMapWorldSpaceTexelSize_{};
};

}
