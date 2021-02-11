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
#include "../RenderPipeline/BatchCompositor.h"

#include <EASTL/vector.h>

namespace Urho3D
{

class Camera;
class Drawable;
class Light;
class LightProcessor;
class Node;
struct PipelineBatchByState;
struct ShadowMap;

/// Class that manages single shadow split processing.
class URHO3D_API ShadowSplitProcessor
{
public:
    /// Construct.
    explicit ShadowSplitProcessor(LightProcessor* owner);
    /// Destruct.
    ~ShadowSplitProcessor();

    /// Initialize split for direction light.
    void InitializeDirectional(
        Camera* cullCamera, const FloatRange& splitRange, const ea::vector<Drawable*>& litGeometries);
    /// Initialize split for spot light.
    void InitializeSpot();
    /// Initialize split for point light.
    void InitializePoint(CubeMapFace face);
    /// Finalize shadow.
    void Finalize(const ShadowMap& shadowMap);

    /// Calculate shadow matrix.
    Matrix4 CalculateShadowMatrix(float subPixelOffset) const;
    /// Sort and return shadow batches. Array is cleared automatically.
    void SortShadowBatches(ea::vector<PipelineBatchByState>& sortedBatches) const;

private:
    /// Initialize base directional camera w/o any focusing and adjusting.
    void InitializeBaseDirectionalCamera(Camera* cullCamera);
    /// Return bounding box of lit geometries in split.
    BoundingBox GetLitBoundingBox(const ea::vector<Drawable*>& litGeometries) const;
    /// Return split bounding box in light space.
    BoundingBox GetSplitBoundingBox(Camera* cullCamera, const ea::vector<Drawable*>& litGeometries) const;
    /// Adjust directional camera to keep texels stable.
    void AdjustDirectionalCamera(const BoundingBox& lightSpaceBoundingBox, float shadowMapSize);

    /// Owner light processor.
    LightProcessor* owner_{};
    /// Light.
    Light* light_{};

public:
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

private:
    /// Finalize shadow camera view after shadow casters and the shadow map are known.
    void FinalizeShadowCamera(Light* light);

};

}
