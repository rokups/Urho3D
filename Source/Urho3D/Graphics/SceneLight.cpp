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

#include "../Precompiled.h"

#include "../Core/Context.h"
#include "../Core/IteratorRange.h"
#include "../Core/WorkQueue.h"
#include "../Math/Polyhedron.h"
#include "../Graphics/Camera.h"
#include "../Graphics/SceneLight.h"
#include "../Graphics/Octree.h"
#include "../Graphics/OctreeQuery.h"
#include "../Graphics/Renderer.h"
#include "../Scene/Node.h"

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

/// Frustum Query for point light.
struct PointLightLitGeometriesQuery : public SphereOctreeQuery
{
    /// Return light sphere for the query.
    static Sphere GetLightSphere(Light* light)
    {
        return Sphere(light->GetNode()->GetWorldPosition(), light->GetRange());
    }

    /// Construct.
    PointLightLitGeometriesQuery(ea::vector<Drawable*>& result, ea::vector<Drawable*>* shadowCasters,
        const SceneDrawableData& transientData, Light* light, unsigned viewMask)
        : SphereOctreeQuery(result, GetLightSphere(light), DRAWABLE_GEOMETRY, viewMask)
        , shadowCasters_(shadowCasters)
        , transientData_(&transientData)
        , lightMask_(light->GetLightMaskEffective())
    {
        if (shadowCasters_)
            shadowCasters_->clear();
    }

    void TestDrawables(Drawable** start, Drawable** end, bool inside) override
    {
        for (Drawable* drawable : MakeIteratorRange(start, end))
        {
            const auto isLitOrShadowCaster = IsLitOrShadowCaster(drawable, inside);
            if (isLitOrShadowCaster.first)
                result_.push_back(drawable);
            if (isLitOrShadowCaster.second)
                shadowCasters_->push_back(drawable);
        }
    }

    /// Return whether the drawable is lit and/or shadow caster.
    ea::pair<bool, bool> IsLitOrShadowCaster(Drawable* drawable, bool inside) const
    {
        const unsigned drawableIndex = drawable->GetDrawableIndex();
        const unsigned traits = transientData_->traits_[drawableIndex];

        const bool isInside = (drawable->GetDrawableFlags() & drawableFlags_)
            && (drawable->GetViewMask() & viewMask_)
            && (inside || sphere_.IsInsideFast(drawable->GetWorldBoundingBox()));
        const bool isLit = isInside
            && (traits & SceneDrawableData::DrawableVisibleGeometry)
            && (drawable->GetLightMask() & lightMask_);
        const bool isShadowCaster = shadowCasters_ && isInside
            && drawable->GetCastShadows()
            && (drawable->GetShadowMask() & lightMask_);
        return { isLit, isShadowCaster };
    }

    /// Result array of shadow casters, if applicable.
    ea::vector<Drawable*>* shadowCasters_{};
    /// Visiblity cache.
    const SceneDrawableData* transientData_{};
    /// Light mask to check.
    unsigned lightMask_{};
};

/// Frustum Query for spot light.
struct SpotLightLitGeometriesQuery : public FrustumOctreeQuery
{
    /// Construct.
    SpotLightLitGeometriesQuery(ea::vector<Drawable*>& result, ea::vector<Drawable*>* shadowCasters,
        const SceneDrawableData& transientData, Light* light, unsigned viewMask)
        : FrustumOctreeQuery(result, light->GetFrustum(), DRAWABLE_GEOMETRY, viewMask)
        , shadowCasters_(shadowCasters)
        , transientData_(&transientData)
        , lightMask_(light->GetLightMaskEffective())
    {
        if (shadowCasters_)
            shadowCasters_->clear();
    }

    void TestDrawables(Drawable** start, Drawable** end, bool inside) override
    {
        for (Drawable* drawable : MakeIteratorRange(start, end))
        {
            const auto isLitOrShadowCaster = IsLitOrShadowCaster(drawable, inside);
            if (isLitOrShadowCaster.first)
                result_.push_back(drawable);
            if (isLitOrShadowCaster.second)
                shadowCasters_->push_back(drawable);
        }
    }

    /// Return whether the drawable is lit and/or shadow caster.
    ea::pair<bool, bool> IsLitOrShadowCaster(Drawable* drawable, bool inside) const
    {
        const unsigned drawableIndex = drawable->GetDrawableIndex();
        const unsigned traits = transientData_->traits_[drawableIndex];

        const bool isInside = (drawable->GetDrawableFlags() & drawableFlags_)
            && (drawable->GetViewMask() & viewMask_)
            && (inside || frustum_.IsInsideFast(drawable->GetWorldBoundingBox()));
        const bool isLit = isInside
            && (traits & SceneDrawableData::DrawableVisibleGeometry)
            && (drawable->GetLightMask() & lightMask_);
        const bool isShadowCaster = shadowCasters_ && isInside
            && drawable->GetCastShadows()
            && (drawable->GetShadowMask() & lightMask_);
        return { isLit, isShadowCaster };
    }

    /// Result array of shadow casters, if applicable.
    ea::vector<Drawable*>* shadowCasters_{};
    /// Visiblity cache.
    const SceneDrawableData* transientData_{};
    /// Light mask to check.
    unsigned lightMask_{};
};

/// %Frustum octree query for directional light shadowcasters.
class DirectionalLightShadowCasterOctreeQuery : public FrustumOctreeQuery
{
public:
    /// Construct with frustum and query parameters.
    DirectionalLightShadowCasterOctreeQuery(ea::vector<Drawable*>& result,
        const Frustum& frustum, DrawableFlags drawableFlags, Light* light, unsigned viewMask)
        : FrustumOctreeQuery(result, frustum, drawableFlags, viewMask)
        , lightMask_(light->GetLightMask())
    {
    }

    /// Intersection test for drawables.
    void TestDrawables(Drawable** start, Drawable** end, bool inside) override
    {
        for (Drawable* drawable : MakeIteratorRange(start, end))
        {
            if (IsShadowCaster(drawable, inside))
                result_.push_back(drawable);
        }
    }

    /// Return whether the drawable is shadow caster.
    bool IsShadowCaster(Drawable* drawable, bool inside) const
    {
        return drawable->GetCastShadows()
            && (drawable->GetDrawableFlags() & drawableFlags_)
            && (drawable->GetViewMask() & viewMask_)
            && (drawable->GetShadowMask() & lightMask_)
            && (inside || frustum_.IsInsideFast(drawable->GetWorldBoundingBox()));
    }

    /// Light mask to check.
    unsigned lightMask_{};
};

/// Return current light fade.
float GetLightFade(Light* light)
{
    const float fadeStart = light->GetFadeDistance();
    const float fadeEnd = light->GetDrawDistance();
    if (light->GetLightType() != LIGHT_DIRECTIONAL && fadeEnd > 0.0f && fadeStart > 0.0f && fadeStart < fadeEnd)
        return ea::min(1.0f - (light->GetDistance() - fadeStart) / (fadeEnd - fadeStart), 1.0f);
    return 1.0f;
}

}

void SceneLightShadowSplit::SetupDirLightShadowCamera(Light* light, Camera* cullCamera,
    const ea::vector<Drawable*>& litGeometries,
    const DrawableZRange& sceneZRange, const ea::vector<DrawableZRange>& drawableZRanges)
{
    Node* shadowCameraNode = shadowCamera_->GetNode();
    Node* lightNode = light->GetNode();
    float extrusionDistance = Min(cullCamera->GetFarClip(), light->GetShadowMaxExtrusion());
    const FocusParameters& parameters = light->GetShadowFocus();

    // Calculate initial position & rotation
    Vector3 pos = cullCamera->GetNode()->GetWorldPosition() - extrusionDistance * lightNode->GetWorldDirection();
    shadowCameraNode->SetTransform(pos, lightNode->GetWorldRotation());

    // Use the scene Z bounds to limit frustum size if applicable
    const DrawableZRange splitZRange = parameters.focus_ ? sceneZRange & zRange_ : zRange_;

    // Calculate main camera shadowed frustum in light's view space
    Frustum splitFrustum = cullCamera->GetSplitFrustum(splitZRange.first, splitZRange.second);
    Polyhedron frustumVolume;
    frustumVolume.Define(splitFrustum);
    // If focusing enabled, clip the frustum volume by the combined bounding box of the lit geometries within the frustum
    if (parameters.focus_)
    {
        BoundingBox litGeometriesBox;
        unsigned lightMask = light->GetLightMaskEffective();

        for (Drawable* drawable : litGeometries)
        {
            const DrawableZRange& drawableZRange = drawableZRanges[drawable->GetDrawableIndex()];
            if (drawableZRange.Interset(splitZRange))
                litGeometriesBox.Merge(drawable->GetWorldBoundingBox());
        }

        if (litGeometriesBox.Defined())
        {
            frustumVolume.Clip(litGeometriesBox);
            // If volume became empty, restore it to avoid zero size
            if (frustumVolume.Empty())
                frustumVolume.Define(splitFrustum);
        }
    }

    // Transform frustum volume to light space
    const Matrix3x4& lightView = shadowCamera_->GetView();
    frustumVolume.Transform(lightView);

    // Fit the frustum volume inside a bounding box. If uniform size, use a sphere instead
    BoundingBox shadowBox;
    if (!parameters.nonUniform_)
        shadowBox.Define(Sphere(frustumVolume));
    else
        shadowBox.Define(frustumVolume);

    shadowCamera_->SetOrthographic(true);
    shadowCamera_->SetAspectRatio(1.0f);
    shadowCamera_->SetNearClip(0.0f);
    shadowCamera_->SetFarClip(shadowBox.max_.z_);

    // Center shadow camera on the bounding box. Can not snap to texels yet as the shadow map viewport is unknown
    shadowMap_.region_ = IntRect::ZERO;
    QuantizeDirLightShadowCamera(parameters, shadowBox);
}

void SceneLightShadowSplit::QuantizeDirLightShadowCamera(const FocusParameters& parameters, const BoundingBox& viewBox)
{
    Node* shadowCameraNode = shadowCamera_->GetNode();
    const auto shadowMapWidth = static_cast<float>(shadowMap_.region_.Width());

    const float minX = viewBox.min_.x_;
    const float minY = viewBox.min_.y_;
    const float maxX = viewBox.max_.x_;
    const float maxY = viewBox.max_.y_;

    const Vector2 center((minX + maxX) * 0.5f, (minY + maxY) * 0.5f);
    Vector2 viewSize(maxX - minX, maxY - minY);

    // Quantize size to reduce swimming
    // Note: if size is uniform and there is no focusing, quantization is unnecessary
    if (parameters.nonUniform_)
    {
        viewSize.x_ = ceilf(sqrtf(viewSize.x_ / parameters.quantize_));
        viewSize.y_ = ceilf(sqrtf(viewSize.y_ / parameters.quantize_));
        viewSize.x_ = Max(viewSize.x_ * viewSize.x_ * parameters.quantize_, parameters.minView_);
        viewSize.y_ = Max(viewSize.y_ * viewSize.y_ * parameters.quantize_, parameters.minView_);
    }
    else if (parameters.focus_)
    {
        viewSize.x_ = Max(viewSize.x_, viewSize.y_);
        viewSize.x_ = ceilf(sqrtf(viewSize.x_ / parameters.quantize_));
        viewSize.x_ = Max(viewSize.x_ * viewSize.x_ * parameters.quantize_, parameters.minView_);
        viewSize.y_ = viewSize.x_;
    }

    shadowCamera_->SetOrthoSize(viewSize);

    // Center shadow camera to the view space bounding box
    const Quaternion rot(shadowCameraNode->GetWorldRotation());
    const Vector3 adjust(center.x_, center.y_, 0.0f);
    shadowCameraNode->Translate(rot * adjust, TS_WORLD);

    // If the shadow map viewport is known, snap to whole texels
    if (shadowMapWidth > 0.0f)
    {
        const Vector3 viewPos(rot.Inverse() * shadowCameraNode->GetWorldPosition());
        // Take into account that shadow map border will not be used
        const float invActualSize = 1.0f / (shadowMapWidth - 2.0f);
        const Vector2 texelSize(viewSize.x_ * invActualSize, viewSize.y_ * invActualSize);
        const Vector3 snap(-fmodf(viewPos.x_, texelSize.x_), -fmodf(viewPos.y_, texelSize.y_), 0.0f);
        shadowCameraNode->Translate(rot * snap, TS_WORLD);
    }
}

void SceneLightShadowSplit::FinalizeShadowCamera(Light* light)
{
    const FocusParameters& parameters = light->GetShadowFocus();
    const auto shadowMapWidth = static_cast<float>(shadowMap_.region_.Width());
    const LightType type = light->GetLightType();

    if (type == LIGHT_DIRECTIONAL)
    {
        BoundingBox shadowBox;
        shadowBox.max_.y_ = shadowCamera_->GetOrthoSize() * 0.5f;
        shadowBox.max_.x_ = shadowCamera_->GetAspectRatio() * shadowBox.max_.y_;
        shadowBox.min_.y_ = -shadowBox.max_.y_;
        shadowBox.min_.x_ = -shadowBox.max_.x_;

        // Requantize and snap to shadow map texels
        QuantizeDirLightShadowCamera(parameters, shadowBox);
    }

    if (type == LIGHT_SPOT && parameters.focus_)
    {
        const float viewSizeX = Max(Abs(shadowCasterBox_.min_.x_), Abs(shadowCasterBox_.max_.x_));
        const float viewSizeY = Max(Abs(shadowCasterBox_.min_.y_), Abs(shadowCasterBox_.max_.y_));
        float viewSize = Max(viewSizeX, viewSizeY);
        // Scale the quantization parameters, because view size is in projection space (-1.0 - 1.0)
        const float invOrthoSize = 1.0f / shadowCamera_->GetOrthoSize();
        const float quantize = parameters.quantize_ * invOrthoSize;
        const float minView = parameters.minView_ * invOrthoSize;

        viewSize = Max(ceilf(viewSize / quantize) * quantize, minView);
        if (viewSize < 1.0f)
            shadowCamera_->SetZoom(1.0f / viewSize);
    }

    // Perform a finalization step for all lights: ensure zoom out of 2 pixels to eliminate border filtering issues
    // For point lights use 4 pixels, as they must not cross sides of the virtual cube map (maximum 3x3 PCF)
    if (shadowCamera_->GetZoom() >= 1.0f)
    {
        if (light->GetLightType() != LIGHT_POINT)
            shadowCamera_->SetZoom(shadowCamera_->GetZoom() * ((shadowMapWidth - 2.0f) / shadowMapWidth));
        else
        {
#ifdef URHO3D_OPENGL
            shadowCamera_->SetZoom(shadowCamera_->GetZoom() * ((shadowMapWidth - 3.0f) / shadowMapWidth));
#else
            shadowCamera_->SetZoom(shadowCamera_->GetZoom() * ((shadowMapWidth - 4.0f) / shadowMapWidth));
#endif
        }
    }
}

Matrix4 SceneLightShadowSplit::CalculateShadowMatrix(float subPixelOffset) const
{
    if (!shadowMap_)
        return Matrix4::IDENTITY;

    const IntRect& viewport = shadowMap_.region_;
    const Matrix3x4& shadowView = shadowCamera_->GetView();
    const Matrix4 shadowProj = shadowCamera_->GetGPUProjection();
    const IntVector2 textureSize = shadowMap_.texture_->GetSize();

    Vector3 offset;
    Vector3 scale;

    // Apply viewport offset and scale
    offset.x_ = static_cast<float>(viewport.left_) / textureSize.x_;
    offset.y_ = static_cast<float>(viewport.top_) / textureSize.y_;
    offset.z_ = 0.0f;
    scale.x_ = 0.5f * static_cast<float>(viewport.Width()) / textureSize.x_;
    scale.y_ = 0.5f * static_cast<float>(viewport.Height()) / textureSize.y_;
    scale.z_ = 1.0f;

    offset.x_ += scale.x_;
    offset.y_ += scale.y_;

    // Apply GAPI-specific transforms
    assert(Graphics::GetPixelUVOffset() == Vector2::ZERO);
#ifdef URHO3D_OPENGL
    offset.z_ = 0.5f;
    scale.z_ = 0.5f;
    offset.y_ = 1.0f - offset.y_;
#else
    scale.y_ = -scale.y_;
#endif

    // Apply sub-pixel offset if necessary
    offset.x_ -= subPixelOffset / textureSize.x_;
    offset.y_ -= subPixelOffset / textureSize.y_;

    // Make final matrix
    Matrix4 texAdjust(Matrix4::IDENTITY);
    texAdjust.SetTranslation(offset);
    texAdjust.SetScale(scale);

    return texAdjust * shadowProj * shadowView;
}

SceneLight::SceneLight(Light* light)
    : light_(light)
{
    for (SceneLightShadowSplit& split : splits_)
        split.sceneLight_ = this;
}

void SceneLight::BeginFrame(bool hasShadow)
{
    litGeometries_.clear();
    tempShadowCasters_.clear();
    shadowMap_ = {};
    hasShadow_ = hasShadow;
    MarkPipelineStateHashDirty();
}

void SceneLight::UpdateLitGeometriesAndShadowCasters(SceneLightProcessContext& ctx)
{
    CollectLitGeometriesAndMaybeShadowCasters(ctx);

    const LightType lightType = light_->GetLightType();
    Camera* cullCamera = ctx.frameInfo_.camera_;
    Octree* octree = ctx.frameInfo_.octree_;
    const Frustum& frustum = cullCamera->GetFrustum();

    if (hasShadow_)
    {
        SetupShadowCameras(ctx);

        // Process each split for shadow casters
        for (unsigned i = 0; i < numSplits_; ++i)
        {
            Camera* shadowCamera = splits_[i].shadowCamera_;
            const Frustum& shadowCameraFrustum = shadowCamera->GetFrustum();
            splits_[i].shadowCasters_.clear();
            splits_[i].shadowCasterBatches_.clear();

            // For point light check that the face is visible: if not, can skip the split
            if (lightType == LIGHT_POINT && frustum.IsInsideFast(BoundingBox(shadowCameraFrustum)) == OUTSIDE)
                continue;

            // For directional light check that the split is inside the visible scene: if not, can skip the split
            if (lightType == LIGHT_DIRECTIONAL)
            {
                if (!ctx.sceneZRange_.Interset(splits_[i].zRange_))
                    continue;

                // Reuse lit geometry query for all except directional lights
                DirectionalLightShadowCasterOctreeQuery query(
                    tempShadowCasters_, shadowCameraFrustum, DRAWABLE_GEOMETRY, light_, cullCamera->GetViewMask());
                octree->GetDrawables(query);
            }

            // Check which shadow casters actually contribute to the shadowing
            ProcessShadowCasters(ctx, tempShadowCasters_, i);
        }
    }
}

void SceneLight::FinalizeShadowMap()
{
    // Skip if doesn't have shadow or shadow casters
    if (!hasShadow_)
        return;

    const auto hasShadowCaster = [](const SceneLightShadowSplit& split) { return !split.shadowCasters_.empty(); };
    if (!ea::any_of(ea::begin(splits_), ea::begin(splits_) + numSplits_, hasShadowCaster))
    {
        hasShadow_ = false;
        return;
    }

    // Evaluate split shadow map size
    // TODO(renderer): Implement me
    shadowMapSplitSize_ = 512;
    shadowMapSize_ = IntVector2{ shadowMapSplitSize_, shadowMapSplitSize_ } * GetSplitsGridSize();
}

void SceneLight::SetShadowMap(const ShadowMap& shadowMap)
{
    // If failed to allocate, reset shadows
    if (!shadowMap.texture_)
    {
        numSplits_ = 0;
        return;
    }

    // Initialize shadow map for all splits
    shadowMap_ = shadowMap;
    for (unsigned splitIndex = 0; splitIndex < numSplits_; ++splitIndex)
    {
        SceneLightShadowSplit& split = splits_[splitIndex];
        split.shadowMap_ = shadowMap.GetSplit(splitIndex, GetSplitsGridSize());
        split.FinalizeShadowCamera(light_);
    }
}

void SceneLight::FinalizeShaderParameters(Camera* cullCamera, float subPixelOffset)
{
    Node* lightNode = light_->GetNode();
    const LightType lightType = light_->GetLightType();

    // Setup common shader parameters
    shaderParams_.position_ = lightNode->GetWorldPosition();
    shaderParams_.direction_ = lightNode->GetWorldRotation() * Vector3::BACK;
    shaderParams_.invRange_ = lightType == LIGHT_DIRECTIONAL ? 0.0f : 1.0f / Max(light_->GetRange(), M_EPSILON);
    shaderParams_.radius_ = light_->GetRadius();
    shaderParams_.length_ = light_->GetLength();

    // Negative lights will use subtract blending, so use absolute RGB values
    const float fade = GetLightFade(light_);
    shaderParams_.color_ = fade * light_->GetEffectiveColor().Abs().ToVector3();
    shaderParams_.specularIntensity_ = fade * light_->GetEffectiveSpecularIntensity();

    // Setup vertex light parameters
    if (lightType == LIGHT_SPOT)
    {
        shaderParams_.cutoff_ = Cos(light_->GetFov() * 0.5f);
        shaderParams_.invCutoff_ = 1.0f / (1.0f - shaderParams_.cutoff_);
    }
    else
    {
        shaderParams_.cutoff_ = -2.0f;
        shaderParams_.invCutoff_ = 1.0f;
    }

    // Skip the rest if no shadow
    if (!shadowMap_)
        return;

    switch (lightType)
    {
    case LIGHT_DIRECTIONAL:
        for (unsigned splitIndex = 0; splitIndex < numSplits_; ++splitIndex)
            shaderParams_.shadowMatrices_[splitIndex] = splits_[splitIndex].CalculateShadowMatrix(subPixelOffset);
        break;

    case LIGHT_SPOT:
        // TODO(renderer): Implement me
        assert(0);
        /*CalculateSpotMatrix(shadowMatrices[0], light);
        bool isShadowed = shadowMap && graphics->HasTextureUnit(TU_SHADOWMAP);
        if (isShadowed)
            CalculateShadowMatrix(shadowMatrices[1], lightQueue_, 0, renderer);*/
        break;

    case LIGHT_POINT:
        // TODO(renderer): Implement me
        assert(0);
        /*Matrix4 lightVecRot(lightNode->GetWorldRotation().RotationMatrix());
        // HLSL compiler will pack the parameters as if the matrix is only 3x4, so must be careful to not overwrite
        // the next parameter
#ifdef URHO3D_OPENGL
        graphics->SetShaderParameter(VSP_LIGHTMATRICES, lightVecRot.Data(), 16);
#else
        graphics->SetShaderParameter(VSP_LIGHTMATRICES, lightVecRot.Data(), 12);
#endif*/
        break;

    default:
        break;
    }

    // TODO(renderer): Implement me
    shaderParams_.shadowCubeAdjust_ = Vector4::ZERO;
    /*// Calculate point light shadow sampling offsets (unrolled cube map)
    auto faceWidth = (unsigned)(shadowMap->GetWidth() / 2);
    auto faceHeight = (unsigned)(shadowMap->GetHeight() / 3);
    auto width = (float)shadowMap->GetWidth();
    auto height = (float)shadowMap->GetHeight();
#ifdef URHO3D_OPENGL
    float mulX = (float)(faceWidth - 3) / width;
    float mulY = (float)(faceHeight - 3) / height;
    float addX = 1.5f / width;
    float addY = 1.5f / height;
#else
    float mulX = (float)(faceWidth - 4) / width;
    float mulY = (float)(faceHeight - 4) / height;
    float addX = 2.5f / width;
    float addY = 2.5f / height;
#endif
    // If using 4 shadow samples, offset the position diagonally by half pixel
    if (renderer->GetShadowQuality() == SHADOWQUALITY_PCF_16BIT || renderer->GetShadowQuality() == SHADOWQUALITY_PCF_24BIT)
    {
        addX -= 0.5f / width;
        addY -= 0.5f / height;
    }
    graphics->SetShaderParameter(PSP_SHADOWCUBEADJUST, Vector4(mulX, mulY, addX, addY));*/

    {
        // Calculate shadow camera depth parameters for point light shadows and shadow fade parameters for
        //  directional light shadows, stored in the same uniform
        Camera* shadowCamera = splits_[0].shadowCamera_;
        const float nearClip = shadowCamera->GetNearClip();
        const float farClip = shadowCamera->GetFarClip();
        const float q = farClip / (farClip - nearClip);
        const float r = -q * nearClip;

        const CascadeParameters& parameters = light_->GetShadowCascade();
        const float viewFarClip = cullCamera->GetFarClip();
        const float shadowRange = parameters.GetShadowRange();
        const float fadeStart = parameters.fadeStart_ * shadowRange / viewFarClip;
        const float fadeEnd = shadowRange / viewFarClip;
        const float fadeRange = fadeEnd - fadeStart;

        shaderParams_.shadowDepthFade_ = { q, r, fadeStart, 1.0f / fadeRange };
    }

    {
        float intensity = light_->GetShadowIntensity();
        const float fadeStart = light_->GetShadowFadeDistance();
        const float fadeEnd = light_->GetShadowDistance();
        if (fadeStart > 0.0f && fadeEnd > 0.0f && fadeEnd > fadeStart)
            intensity =
                Lerp(intensity, 1.0f, Clamp((light_->GetDistance() - fadeStart) / (fadeEnd - fadeStart), 0.0f, 1.0f));
        const float pcfValues = (1.0f - intensity);
        float samples = 1.0f;
        // TODO(renderer): Support me
        //if (renderer->GetShadowQuality() == SHADOWQUALITY_PCF_16BIT || renderer->GetShadowQuality() == SHADOWQUALITY_PCF_24BIT)
        //    samples = 4.0f;
        shaderParams_.shadowIntensity_ = { pcfValues / samples, intensity, 0.0f, 0.0f };
    }

    const float sizeX = 1.0f / static_cast<float>(shadowMap_.texture_->GetWidth());
    const float sizeY = 1.0f / static_cast<float>(shadowMap_.texture_->GetHeight());
    shaderParams_.shadowMapInvSize_ = { sizeX, sizeY };

    shaderParams_.shadowSplits_ = { M_LARGE_VALUE, M_LARGE_VALUE, M_LARGE_VALUE, M_LARGE_VALUE };
    if (numSplits_ > 1)
        shaderParams_.shadowSplits_.x_ = splits_[0].zRange_.second / cullCamera->GetFarClip();
    if (numSplits_ > 2)
        shaderParams_.shadowSplits_.y_ = splits_[1].zRange_.second / cullCamera->GetFarClip();
    if (numSplits_ > 3)
        shaderParams_.shadowSplits_.z_ = splits_[2].zRange_.second / cullCamera->GetFarClip();

    // TODO(renderer): Implement me
    shaderParams_.normalOffsetScale_ = Vector4::ZERO;
    /*if (light->GetShadowBias().normalOffset_ > 0.0f)
    {
        Vector4 normalOffsetScale(Vector4::ZERO);

        // Scale normal offset strength with the width of the shadow camera view
        if (light->GetLightType() != LIGHT_DIRECTIONAL)
        {
            Camera* shadowCamera = lightQueue_->shadowSplits_[0].shadowCamera_;
            normalOffsetScale.x_ = 2.0f * tanf(shadowCamera->GetFov() * M_DEGTORAD * 0.5f) * shadowCamera->GetFarClip();
        }
        else
        {
            normalOffsetScale.x_ = lightQueue_->shadowSplits_[0].shadowCamera_->GetOrthoSize();
            if (lightQueue_->shadowSplits_.size() > 1)
                normalOffsetScale.y_ = lightQueue_->shadowSplits_[1].shadowCamera_->GetOrthoSize();
            if (lightQueue_->shadowSplits_.size() > 2)
                normalOffsetScale.z_ = lightQueue_->shadowSplits_[2].shadowCamera_->GetOrthoSize();
            if (lightQueue_->shadowSplits_.size() > 3)
                normalOffsetScale.w_ = lightQueue_->shadowSplits_[3].shadowCamera_->GetOrthoSize();
        }

        normalOffsetScale *= light->GetShadowBias().normalOffset_;
#ifdef GL_ES_VERSION_2_0
        normalOffsetScale *= renderer->GetMobileNormalOffsetMul();
#endif
    }*/
}

unsigned SceneLight::RecalculatePipelineStateHash() const
{
    const BiasParameters& biasParameters = light_->GetShadowBias();

    // TODO(renderer): Extract into pipeline state factory
    unsigned hash = 0;
    hash |= light_->GetLightType() & 0x3;
    hash |= static_cast<unsigned>(hasShadow_) << 2;
    hash |= static_cast<unsigned>(!!light_->GetShapeTexture()) << 3;
    hash |= static_cast<unsigned>(light_->GetSpecularIntensity() > 0.0f) << 4;
    hash |= static_cast<unsigned>(biasParameters.normalOffset_ > 0.0f) << 5;
    CombineHash(hash, biasParameters.constantBias_);
    CombineHash(hash, biasParameters.slopeScaledBias_);
    return hash;
}

void SceneLight::CollectLitGeometriesAndMaybeShadowCasters(SceneLightProcessContext& ctx)
{
    Octree* octree = ctx.frameInfo_.octree_;
    switch (light_->GetLightType())
    {
    case LIGHT_SPOT:
    {
        SpotLightLitGeometriesQuery query(litGeometries_, hasShadow_ ? &tempShadowCasters_ : nullptr,
            *ctx.drawableData_, light_, ctx.frameInfo_.camera_->GetViewMask());
        octree->GetDrawables(query);
        break;
    }
    case LIGHT_POINT:
    {
        PointLightLitGeometriesQuery query(litGeometries_, hasShadow_ ? &tempShadowCasters_ : nullptr,
            *ctx.drawableData_, light_, ctx.frameInfo_.camera_->GetViewMask());
        octree->GetDrawables(query);
        break;
    }
    case LIGHT_DIRECTIONAL:
    {
        const unsigned lightMask = light_->GetLightMask();
        ctx.visibleGeometries_->ForEach([&](unsigned, unsigned, Drawable* drawable)
        {
            if (drawable->GetLightMask() & lightMask)
                litGeometries_.push_back(drawable);
        });
        break;
    }
    }
}

Camera* SceneLight::GetOrCreateShadowCamera(unsigned split)
{
    if (!splits_[split].shadowCamera_)
    {
        auto node = MakeShared<Node>(light_->GetContext());
        auto camera = node->CreateComponent<Camera>();
        camera->SetOrthographic(false);
        camera->SetZoom(1.0f);
        splits_[split].shadowCameraNode_ = node;
        splits_[split].shadowCamera_ = camera;
    }
    return splits_[split].shadowCamera_;
}

void SceneLight::SetupShadowCameras(SceneLightProcessContext& ctx)
{
    Camera* cullCamera = ctx.frameInfo_.camera_;

    switch (light_->GetLightType())
    {
    case LIGHT_DIRECTIONAL:
    {
        const CascadeParameters& cascade = light_->GetShadowCascade();

        float nearSplit = cullCamera->GetNearClip();
        const int numSplits = light_->GetNumShadowSplits();

        numSplits_ = 0;
        for (unsigned i = 0; i < numSplits; ++i)
        {
            // If split is completely beyond camera far clip, we are done
            if (nearSplit > cullCamera->GetFarClip())
                break;

            const float farSplit = Min(cullCamera->GetFarClip(), cascade.splits_[i]);
            if (farSplit <= nearSplit)
                break;

            // Setup the shadow camera for the split
            Camera* shadowCamera = GetOrCreateShadowCamera(i);
            splits_[i].zRange_ = { nearSplit, farSplit };
            splits_[i].SetupDirLightShadowCamera(light_, ctx.frameInfo_.camera_, litGeometries_,
                ctx.sceneZRange_, ctx.drawableData_->zRange_);

            nearSplit = farSplit;
            ++numSplits_;
        }
        break;
    }
    case LIGHT_SPOT:
    {
        Camera* shadowCamera = GetOrCreateShadowCamera(0);
        Node* cameraNode = shadowCamera->GetNode();
        Node* lightNode = light_->GetNode();

        cameraNode->SetTransform(lightNode->GetWorldPosition(), lightNode->GetWorldRotation());
        shadowCamera->SetNearClip(light_->GetShadowNearFarRatio() * light_->GetRange());
        shadowCamera->SetFarClip(light_->GetRange());
        shadowCamera->SetFov(light_->GetFov());
        shadowCamera->SetAspectRatio(light_->GetAspectRatio());

        numSplits_ = 1;
        break;
    }
    case LIGHT_POINT:
    {
        static const Vector3* directions[] =
        {
            &Vector3::RIGHT,
            &Vector3::LEFT,
            &Vector3::UP,
            &Vector3::DOWN,
            &Vector3::FORWARD,
            &Vector3::BACK
        };

        for (unsigned i = 0; i < MAX_CUBEMAP_FACES; ++i)
        {
            Camera* shadowCamera = GetOrCreateShadowCamera(i);
            Node* cameraNode = shadowCamera->GetNode();

            // When making a shadowed point light, align the splits along X, Y and Z axes regardless of light rotation
            cameraNode->SetPosition(light_->GetNode()->GetWorldPosition());
            cameraNode->SetDirection(*directions[i]);
            shadowCamera->SetNearClip(light_->GetShadowNearFarRatio() * light_->GetRange());
            shadowCamera->SetFarClip(light_->GetRange());
            shadowCamera->SetFov(90.0f);
            shadowCamera->SetAspectRatio(1.0f);
        }

        numSplits_ = MAX_CUBEMAP_FACES;
        break;
    }
    }
}

bool SceneLight::IsShadowCasterVisible(SceneLightProcessContext& ctx,
    Drawable* drawable, BoundingBox lightViewBox, Camera* shadowCamera, const Matrix3x4& lightView,
    const Frustum& lightViewFrustum, const BoundingBox& lightViewFrustumBox)
{
    if (shadowCamera->IsOrthographic())
    {
        // Extrude the light space bounding box up to the far edge of the frustum's light space bounding box
        lightViewBox.max_.z_ = Max(lightViewBox.max_.z_, lightViewFrustumBox.max_.z_);
        return lightViewFrustum.IsInsideFast(lightViewBox) != OUTSIDE;
    }
    else
    {
        // If light is not directional, can do a simple check: if object is visible, its shadow is too
        const unsigned drawableIndex = drawable->GetDrawableIndex();
        if (ctx.drawableData_->traits_[drawableIndex] & SceneDrawableData::DrawableVisibleGeometry)
            return true;

        // For perspective lights, extrusion direction depends on the position of the shadow caster
        Vector3 center = lightViewBox.Center();
        Ray extrusionRay(center, center);

        float extrusionDistance = shadowCamera->GetFarClip();
        float originalDistance = Clamp(center.Length(), M_EPSILON, extrusionDistance);

        // Because of the perspective, the bounding box must also grow when it is extruded to the distance
        float sizeFactor = extrusionDistance / originalDistance;

        // Calculate the endpoint box and merge it to the original. Because it's axis-aligned, it will be larger
        // than necessary, so the test will be conservative
        Vector3 newCenter = extrusionDistance * extrusionRay.direction_;
        Vector3 newHalfSize = lightViewBox.Size() * sizeFactor * 0.5f;
        BoundingBox extrudedBox(newCenter - newHalfSize, newCenter + newHalfSize);
        lightViewBox.Merge(extrudedBox);

        return lightViewFrustum.IsInsideFast(lightViewBox) != OUTSIDE;
    }
}

void SceneLight::ProcessShadowCasters(SceneLightProcessContext& ctx,
    const ea::vector<Drawable*>& drawables, unsigned splitIndex)
{
    const unsigned workerThreadIndex = WorkQueue::GetWorkerThreadIndex();
    unsigned lightMask = light_->GetLightMaskEffective();
    Camera* cullCamera = ctx.frameInfo_.camera_;

    Camera* shadowCamera = splits_[splitIndex].shadowCamera_;
    const Frustum& shadowCameraFrustum = shadowCamera->GetFrustum();
    const Matrix3x4& lightView = shadowCamera->GetView();
    const Matrix4& lightProj = shadowCamera->GetProjection();
    LightType type = light_->GetLightType();

    splits_[splitIndex].shadowCasterBox_.Clear();

    // Transform scene frustum into shadow camera's view space for shadow caster visibility check. For point & spot lights,
    // we can use the whole scene frustum. For directional lights, use the intersection of the scene frustum and the split
    // frustum, so that shadow casters do not get rendered into unnecessary splits
    Frustum lightViewFrustum;
    if (type != LIGHT_DIRECTIONAL)
        lightViewFrustum = cullCamera->GetSplitFrustum(ctx.sceneZRange_.first, ctx.sceneZRange_.second).Transformed(lightView);
    else
    {
        const DrawableZRange splitZRange = ctx.sceneZRange_ & splits_[splitIndex].zRange_;
        lightViewFrustum = cullCamera->GetSplitFrustum(splitZRange.first, splitZRange.second).Transformed(lightView);
    }

    BoundingBox lightViewFrustumBox(lightViewFrustum);

    // Check for degenerate split frustum: in that case there is no need to get shadow casters
    if (lightViewFrustum.vertices_[0] == lightViewFrustum.vertices_[4])
        return;

    BoundingBox lightViewBox;
    BoundingBox lightProjBox;

    for (auto i = drawables.begin(); i != drawables.end(); ++i)
    {
        Drawable* drawable = *i;
        // In case this is a point or spot light query result reused for optimization, we may have non-shadowcasters included.
        // Check for that first
        if (!drawable->GetCastShadows())
            continue;
        // Check shadow mask
        if (!(drawable->GetShadowMask() & lightMask))
            continue;
        // For point light, check that this drawable is inside the split shadow camera frustum
        if (type == LIGHT_POINT && shadowCameraFrustum.IsInsideFast(drawable->GetWorldBoundingBox()) == OUTSIDE)
            continue;

        // Check shadow distance
        // Note: as lights are processed threaded, it is possible a drawable's UpdateBatches() function is called several
        // times. However, this should not cause problems as no scene modification happens at this point.
        const unsigned drawableIndex = drawable->GetDrawableIndex();
        const bool isUpdated = ctx.drawableData_->isUpdated_[drawableIndex].test_and_set(std::memory_order_relaxed);
        if (!isUpdated)
            ctx.geometriesToBeUpdates_->Insert(workerThreadIndex, drawable);

        // Project shadow caster bounding box to light view space for visibility check
        lightViewBox = drawable->GetWorldBoundingBox().Transformed(lightView);

        if (IsShadowCasterVisible(ctx, drawable, lightViewBox, shadowCamera, lightView, lightViewFrustum, lightViewFrustumBox))
        {
            // Merge to shadow caster bounding box (only needed for focused spot lights) and add to the list
            if (type == LIGHT_SPOT && light_->GetShadowFocus().focus_)
            {
                lightProjBox = lightViewBox.Projected(lightProj);
                splits_[splitIndex].shadowCasterBox_.Merge(lightProjBox);
            }

            splits_[splitIndex].shadowCasters_.push_back(drawable);
        }
    }
}

IntVector2 SceneLight::GetSplitsGridSize() const
{
    if (numSplits_ == 1)
        return { 1, 1 };
    else if (numSplits_ == 2)
        return { 2, 1 };
    else if (numSplits_ < 6)
        return { 2, 2 };
    else
        return { 3, 2 };
}

}
