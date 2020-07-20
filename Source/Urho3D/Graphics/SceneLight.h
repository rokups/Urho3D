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

#include "../Core/ThreadedVector.h"
#include "../Graphics/Light.h"
#include "../Graphics/SceneDrawableData.h"
#include "../Graphics/PipelineStateTracker.h"

#include <EASTL/vector.h>

namespace Urho3D
{

/// Per-viewport light data.
class SceneLight : public PipelineStateTracker
{
public:
    /// Construct.
    explicit SceneLight(Light* light) : light_(light) {}

    /// Clear in the beginning of the frame.
    void BeginFrame(bool hasShadow);
    /// Process light in working thread.
    void Process(Octree* octree, Camera* cullCamera, const DrawableZRange& sceneZRange,
        const ThreadedVector<Drawable*>& visibleGeometries, SceneDrawableData& drawableData);

    /// Return light.
    Light* GetLight() const { return light_; }
    /// Return lit geometries.
    const ea::vector<Drawable*>& GetLitGeometries() const { return litGeometries_; };

private:
    /// Recalculate hash. Shall be save to call from multiple threads as long as the object is not changing.
    unsigned RecalculatePipelineStateHash() const override;
    /// Return or create shadow camera for split.
    Camera* GetOrCreateShadowCamera(unsigned split);
    /// Setup shadow cameras.
    void SetupShadowCameras(Camera* cullCamera, const DrawableZRange& sceneZRange,
        const ThreadedVector<Drawable*>& visibleGeometries, SceneDrawableData& drawableData);
    /// Setup shadow camera for directional light split.
    void SetupDirLightShadowCamera(Camera* cullCamera, Camera* shadowCamera,
        Light* light, float nearSplit, float farSplit, const DrawableZRange& sceneZRange,
        const ThreadedVector<Drawable*>& visibleGeometries, SceneDrawableData& drawableData);
    /// Quantize a directional light shadow camera view to eliminate swimming.
    void QuantizeDirLightShadowCamera(Camera* shadowCamera, Light* light, const IntRect& shadowViewport,
        const BoundingBox& viewBox);

    /// Light.
    Light* light_{};
    /// Whether the light has shadow.
    bool hasShadow_{};

    /// Lit geometries.
    // TODO(renderer): Skip unlit geometries?
    ea::vector<Drawable*> litGeometries_;

    /// Shadow casters.
    ea::vector<Drawable*> shadowCasters_;
    /// Shadow camera nodes.
    SharedPtr<Node> shadowCameraNodes_[MAX_LIGHT_SPLITS];
    /// Shadow cameras.
    SharedPtr<Camera> shadowCameras_[MAX_LIGHT_SPLITS];
    /// Shadow caster start indices.
    unsigned shadowCasterBegin_[MAX_LIGHT_SPLITS]{};
    /// Shadow caster end indices.
    unsigned shadowCasterEnd_[MAX_LIGHT_SPLITS]{};
    /// Combined bounding box of shadow casters in light projection space. Only used for focused spot lights.
    BoundingBox shadowCasterBox_[MAX_LIGHT_SPLITS]{};
    /// Shadow camera near splits (directional lights only).
    float shadowNearSplits_[MAX_LIGHT_SPLITS]{};
    /// Shadow camera far splits (directional lights only).
    float shadowFarSplits_[MAX_LIGHT_SPLITS]{};
    /// Shadow map split count.
    unsigned numSplits_{};
};

}
