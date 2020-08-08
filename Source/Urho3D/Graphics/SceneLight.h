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
#include "../Graphics/SceneBatch.h"
#include "../Graphics/SceneDrawableData.h"
#include "../Graphics/ShadowMapAllocator.h"
#include "../Graphics/PipelineStateTracker.h"
#include "../Graphics/Camera.h"
#include "../Scene/Node.h"

#include <EASTL/vector.h>

namespace Urho3D
{

class SceneLight;

/// Scene light shadow parameters.
struct SceneLightShaderParameters
{
    /// Light direction.
    Vector3 direction_;
    /// Light position.
    Vector3 position_;
    /// Inverse range.
    float invRange_{};

    /// Shadow matrices for each split.
    Matrix4 shadowMatrices_[MAX_CASCADE_SPLITS];
    /// Spot/point light matrix for cookie.
    Matrix4 spotMatrix_;

    /// Light color (faded).
    Vector3 color_;
    /// Specular intensity (faded).
    float specularIntensity_{};

    /// Light radius for volumetric lights.
    float radius_{};
    /// Light length for volumetric lights.
    float length_{};

    /// Shadow cube adjustment.
    Vector4 shadowCubeAdjust_;
    /// Shadow depth fade parameters.
    Vector4 shadowDepthFade_;
    /// Shadow intensity parameters.
    Vector4 shadowIntensity_;
    /// Inverse size of shadowmap.
    Vector2 shadowMapInvSize_;
    /// Shadow splits distances.
    Vector4 shadowSplits_;
    /// Normal offset and scale.
    Vector4 normalOffsetScale_;

    /// Cutoff for vertex lighting.
    float cutoff_{};
    /// Inverse cutoff for vertex lighting.
    float invCutoff_{};
};

/// Scene light processing context.
struct SceneLightProcessContext
{
    /// Frame info.
    FrameInfo frameInfo_;
    /// Z range of visible scene.
    DrawableZRange sceneZRange_{};
    /// All visible geometries.
    const ThreadedVector<Drawable*>* visibleGeometries_{};
    /// Drawable data.
    SceneDrawableData* drawableData_{};
    /// Geometries that has to be updated.
    ThreadedVector<Drawable*>* geometriesToBeUpdates_{};
};

/// Scene light shadow split.
struct SceneLightShadowSplit
{
    /// Owner light.
    SceneLight* sceneLight_{};
    /// Shadow camera node.
    SharedPtr<Node> shadowCameraNode_;
    /// Shadow camera.
    SharedPtr<Camera> shadowCamera_;
    /// Shadow casters.
    ea::vector<Drawable*> shadowCasters_;
    /// Shadow caster batches.
    ea::vector<BaseSceneBatch> shadowCasterBatches_;
    /// Combined bounding box of shadow casters in light projection space. Only used for focused spot lights.
    BoundingBox shadowCasterBox_{};
    /// Shadow camera Z range (directional lights only).
    DrawableZRange zRange_{};
    /// Shadow map for split.
    ShadowMap shadowMap_;

    /// Setup shadow camera for directional light split.
    void SetupDirLightShadowCamera(Light* light, Camera* cullCamera, const ea::vector<Drawable*>& litGeometries,
        const DrawableZRange& sceneZRange, const ea::vector<DrawableZRange>& drawableZRanges);
    /// Quantize a directional light shadow camera view to eliminate swimming.
    void QuantizeDirLightShadowCamera(const FocusParameters& parameters, const BoundingBox& viewBox);
    /// Finalize shadow camera view after shadow casters and the shadow map are known.
    void FinalizeShadowCamera(Light* light);
    /// Calculate shadow matrix.
    Matrix4 CalculateShadowMatrix(float subPixelOffset) const;
};

/// Per-viewport light data.
class SceneLight : public PipelineStateTracker
{
public:
    /// Construct.
    explicit SceneLight(Light* light);

    /// Clear in the beginning of the frame.
    void BeginFrame(bool hasShadow);
    /// Update lit geometries and shadow casters. May be called from worker thread.
    void UpdateLitGeometriesAndShadowCasters(SceneLightProcessContext& ctx);
    /// Finalize shadow basing on shadow caster information.
    void FinalizeShadowMap();
    /// Set shadow map and finalize shader parameters.
    void SetShadowMap(const ShadowMap& shadowMap);
    /// Finalize light and shadow shader parameters.
    void FinalizeShaderParameters(Camera* cullCamera, float subPixelOffset);

    /// Return light.
    Light* GetLight() const { return light_; }
    /// Return whether has shadow.
    bool HasShadow() const { return hasShadow_; }
    /// Return shadow map size.
    IntVector2 GetShadowMapSize() const { return hasShadow_ ? shadowMapSize_ : IntVector2::ZERO; }
    /// Return shadow map.
    ShadowMap GetShadowMap() const { return shadowMap_; }
    /// Return number of splits.
    unsigned GetNumSplits() const { return numSplits_; }
    /// Return shadow split.
    const SceneLightShadowSplit& GetSplit(unsigned splitIndex) const { return splits_[splitIndex]; }
    /// Return mutable shadow split.
    SceneLightShadowSplit& GetMutableSplit(unsigned splitIndex) { return splits_[splitIndex]; }
    /// Return shader parameters.
    const SceneLightShaderParameters& GetShaderParams() const { return shaderParams_; }

    /// Return lit geometries.
    const ea::vector<Drawable*>& GetLitGeometries() const { return litGeometries_; }
    /// Return shadow casters for given split.
    const ea::vector<Drawable*>& GetShadowCasters(unsigned splitIndex) const { return splits_[splitIndex].shadowCasters_; }
    /// Return mutable shadow batches for given split.
    ea::vector<BaseSceneBatch>& GetMutableShadowBatches(unsigned splitIndex) { return splits_[splitIndex].shadowCasterBatches_; }
    /// Return shadow batches for given split.
    const ea::vector<BaseSceneBatch>& GetShadowBatches(unsigned splitIndex) const { return splits_[splitIndex].shadowCasterBatches_; }

private:
    /// Recalculate hash. Shall be save to call from multiple threads as long as the object is not changing.
    unsigned RecalculatePipelineStateHash() const override;
    /// Collect lit geometries (for all light types) and shadow casters (for shadowed spot and point lights).
    void CollectLitGeometriesAndMaybeShadowCasters(SceneLightProcessContext& ctx);
    /// Return or create shadow camera for split.
    Camera* GetOrCreateShadowCamera(unsigned split);
    /// Setup shadow cameras.
    void SetupShadowCameras(SceneLightProcessContext& ctx);
    /// Check visibility of one shadow caster.
    bool IsShadowCasterVisible(SceneLightProcessContext& ctx,
        Drawable* drawable, BoundingBox lightViewBox, Camera* shadowCamera, const Matrix3x4& lightView,
        const Frustum& lightViewFrustum, const BoundingBox& lightViewFrustumBox);
    /// Process shadow casters' visibilities and build their combined view- or projection-space bounding box.
    void ProcessShadowCasters(SceneLightProcessContext& ctx,
        const ea::vector<Drawable*>& drawables, unsigned splitIndex);
    /// Return dimensions of splits grid in shadow map.
    IntVector2 GetSplitsGridSize() const;

    /// Light.
    Light* light_{};
    /// Whether the light has shadow.
    bool hasShadow_{};
    /// Shadow map split size.
    int shadowMapSplitSize_{};
    /// Shadow map size.
    IntVector2 shadowMapSize_{};

    /// Lit geometries.
    // TODO(renderer): Skip unlit geometries?
    ea::vector<Drawable*> litGeometries_;
    /// Shadow caster candidates.
    /// Point and spot lights: all possible shadow casters.
    /// Directional lights: all possible shadows casters for currently processed split
    ea::vector<Drawable*> tempShadowCasters_;

    /// Splits.
    SceneLightShadowSplit splits_[MAX_LIGHT_SPLITS];
    /// Shadow map split count.
    unsigned numSplits_{};

    /// Shadow map allocated to this light.
    ShadowMap shadowMap_;
    /// Shader parameters.
    SceneLightShaderParameters shaderParams_;
};

}
