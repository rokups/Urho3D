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
#include "../Graphics/Octree.h"
#include "../Graphics/OctreeQuery.h"
#include "../Graphics/Renderer.h"
#include "../RenderPipeline/LightProcessor.h"
#include "../Scene/Node.h"

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

/// Cube shadow map padding, in pixels.
const float cubeShadowMapPadding = 2.0f;

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
        const DrawableProcessor* drawableProcessor, Light* light, unsigned viewMask)
        : SphereOctreeQuery(result, GetLightSphere(light), DRAWABLE_GEOMETRY, viewMask)
        , shadowCasters_(shadowCasters)
        , drawableProcessor_(drawableProcessor)
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
        const unsigned geometryFlags = drawableProcessor_->GetGeometryRenderFlags(drawableIndex);

        const bool isInside = (drawable->GetDrawableFlags() & drawableFlags_)
            && (drawable->GetViewMask() & viewMask_)
            && (inside || sphere_.IsInsideFast(drawable->GetWorldBoundingBox()));
        const bool isLit = isInside
            && (geometryFlags & GeometryRenderFlag::Visible)
            && (drawable->GetLightMaskInZone() & lightMask_);
        const bool isShadowCaster = shadowCasters_ && isInside
            && drawable->GetCastShadows()
            && (drawable->GetShadowMask() & lightMask_);
        return { isLit, isShadowCaster };
    }

    /// Result array of shadow casters, if applicable.
    ea::vector<Drawable*>* shadowCasters_{};
    /// Visiblity cache.
    const DrawableProcessor* drawableProcessor_{};
    /// Light mask to check.
    unsigned lightMask_{};
};

/// Frustum Query for spot light.
struct SpotLightLitGeometriesQuery : public FrustumOctreeQuery
{
    /// Construct.
    SpotLightLitGeometriesQuery(ea::vector<Drawable*>& result, ea::vector<Drawable*>* shadowCasters,
        const DrawableProcessor* drawableProcessor, Light* light, unsigned viewMask)
        : FrustumOctreeQuery(result, light->GetFrustum(), DRAWABLE_GEOMETRY, viewMask)
        , shadowCasters_(shadowCasters)
        , drawableProcessor_(drawableProcessor)
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
        const unsigned geometryFlags = drawableProcessor_->GetGeometryRenderFlags(drawableIndex);

        const bool isInside = (drawable->GetDrawableFlags() & drawableFlags_)
            && (drawable->GetViewMask() & viewMask_)
            && (inside || frustum_.IsInsideFast(drawable->GetWorldBoundingBox()));
        const bool isLit = isInside
            && (geometryFlags & GeometryRenderFlag::Visible)
            && (drawable->GetLightMaskInZone() & lightMask_);
        const bool isShadowCaster = shadowCasters_ && isInside
            && drawable->GetCastShadows()
            && (drawable->GetShadowMask() & lightMask_);
        return { isLit, isShadowCaster };
    }

    /// Result array of shadow casters, if applicable.
    ea::vector<Drawable*>* shadowCasters_{};
    /// Visiblity cache.
    const DrawableProcessor* drawableProcessor_{};
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

/// Return spot light matrix.
Matrix4 CalculateSpotMatrix(Light* light)
{
    Node* lightNode = light->GetNode();
    const Matrix3x4 spotView = Matrix3x4(lightNode->GetWorldPosition(), lightNode->GetWorldRotation(), 1.0f).Inverse();

    // Make the projected light slightly smaller than the shadow map to prevent light spill
    Matrix4 spotProj = Matrix4::ZERO;
    const float h = 1.005f / tanf(light->GetFov() * M_DEGTORAD * 0.5f);
    const float w = h / light->GetAspectRatio();
    spotProj.m00_ = w;
    spotProj.m11_ = h;
    spotProj.m22_ = 1.0f / Max(light->GetRange(), M_EPSILON);
    spotProj.m32_ = 1.0f;

    Matrix4 texAdjust;
#ifdef URHO3D_OPENGL
    texAdjust.SetTranslation(Vector3(0.5f, 0.5f, 0.5f));
    texAdjust.SetScale(Vector3(0.5f, -0.5f, 0.5f));
#else
    texAdjust.SetTranslation(Vector3(0.5f, 0.5f, 0.0f));
    texAdjust.SetScale(Vector3(0.5f, -0.5f, 1.0f));
#endif

    return texAdjust * spotProj * spotView;
}

}

void SceneLightShadowSplit::SetupDirLightShadowCamera(Light* light, Camera* cullCamera,
    const ea::vector<Drawable*>& litGeometries, const DrawableProcessor* drawableProcessor)
{
    Node* shadowCameraNode = shadowCamera_->GetNode();
    Node* lightNode = light->GetNode();
    float extrusionDistance = Min(cullCamera->GetFarClip(), light->GetShadowMaxExtrusion());
    const FocusParameters& parameters = light->GetShadowFocus();
    const FloatRange sceneZRange = drawableProcessor->GetSceneZRange();

    // Calculate initial position & rotation
    Vector3 pos = cullCamera->GetNode()->GetWorldPosition() - extrusionDistance * lightNode->GetWorldDirection();
    shadowCameraNode->SetTransform(pos, lightNode->GetWorldRotation());

    // Use the scene Z bounds to limit frustum size if applicable
    const FloatRange splitZRange = parameters.focus_ ? (sceneZRange & zRange_) : zRange_;

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
            const FloatRange& geometryZRange = drawableProcessor->GetGeometryZRange(drawable->GetDrawableIndex());
            if (geometryZRange.Interset(splitZRange))
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

    // TODO(renderer): This code is to be removed. Clean up dependencies.
    /*if (type == LIGHT_SPOT && parameters.focus_)
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
    }*/

    // Perform a finalization step for all lights: ensure zoom out of 2 pixels to eliminate border filtering issues
    // For point lights use 4 pixels, as they must not cross sides of the virtual cube map (maximum 3x3 PCF)
    if (shadowCamera_->GetZoom() >= 1.0f)
    {
        if (light->GetLightType() != LIGHT_POINT)
            shadowCamera_->SetZoom(shadowCamera_->GetZoom() * ((shadowMapWidth - 2.0f) / shadowMapWidth));
        else
        {
            const float scale = (shadowMapWidth - 2.0f * cubeShadowMapPadding) / shadowMapWidth;
            shadowCamera_->SetZoom(shadowCamera_->GetZoom() * scale);
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

LightProcessor::LightProcessor(Light* light)
    : light_(light)
{
    for (SceneLightShadowSplit& split : splits_)
        split.sceneLight_ = this;
}

void LightProcessor::BeginFrame(bool hasShadow)
{
    litGeometries_.clear();
    tempShadowCasters_.clear();
    shadowMap_ = {};
    hasShadow_ = hasShadow;
    MarkPipelineStateHashDirty();
}

void LightProcessor::UpdateLitGeometriesAndShadowCasters(SceneLightProcessContext& ctx)
{
    CollectLitGeometriesAndMaybeShadowCasters(ctx);

    const LightType lightType = light_->GetLightType();
    Camera* cullCamera = ctx.frameInfo_.camera_;
    Octree* octree = ctx.frameInfo_.octree_;
    const Frustum& frustum = cullCamera->GetFrustum();
    const FloatRange& sceneZRange = ctx.dp_->GetSceneZRange();

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
                if (!sceneZRange.Interset(splits_[i].zRange_))
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

void LightProcessor::FinalizeShadowMap()
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
    shadowMapSplitSize_ = light_->GetLightType() != LIGHT_POINT ? 512 : 256;
    shadowMapSize_ = IntVector2{ shadowMapSplitSize_, shadowMapSplitSize_ } * GetSplitsGridSize();
}

void LightProcessor::SetShadowMap(const ShadowMap& shadowMap)
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

void LightProcessor::FinalizeShaderParameters(Camera* cullCamera, float subPixelOffset)
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

    // TODO(renderer): Skip this step if there's no cookies
    switch (lightType)
    {
    case LIGHT_DIRECTIONAL:
        shaderParams_.numLightMatrices_ = 0;
        break;
    case LIGHT_SPOT:
        shaderParams_.lightMatrices_[0] = CalculateSpotMatrix(light_);
        shaderParams_.numLightMatrices_ = 1;
        break;
    case LIGHT_POINT:
        shaderParams_.lightMatrices_[0] = lightNode->GetWorldRotation().RotationMatrix();
        shaderParams_.numLightMatrices_ = 1;
        break;
    default:
        break;
    }

    // Skip the rest if no shadow
    if (!shadowMap_)
        return;

    // Initialize size of shadow map
    const float textureSizeX = static_cast<float>(shadowMap_.texture_->GetWidth());
    const float textureSizeY = static_cast<float>(shadowMap_.texture_->GetHeight());
    shaderParams_.shadowMapInvSize_ = { 1.0f / textureSizeX, 1.0f / textureSizeY };

    shaderParams_.shadowCubeUVBias_ = Vector2::ZERO;
    shaderParams_.shadowCubeAdjust_ = Vector4::ZERO;
    switch (lightType)
    {
    case LIGHT_DIRECTIONAL:
        shaderParams_.numLightMatrices_ = MAX_CASCADE_SPLITS;
        for (unsigned splitIndex = 0; splitIndex < numSplits_; ++splitIndex)
            shaderParams_.lightMatrices_[splitIndex] = splits_[splitIndex].CalculateShadowMatrix(subPixelOffset);
        break;

    case LIGHT_SPOT:
        shaderParams_.numLightMatrices_ = 2;
        shaderParams_.lightMatrices_[1] = splits_[0].CalculateShadowMatrix(subPixelOffset);
        break;

    case LIGHT_POINT:
    {
        const auto& splitViewport = splits_[0].shadowMap_.region_;
        const float viewportSizeX = static_cast<float>(splitViewport.Width());
        const float viewportSizeY = static_cast<float>(splitViewport.Height());
        const float viewportOffsetX = static_cast<float>(splitViewport.Left());
        const float viewportOffsetY = static_cast<float>(splitViewport.Top());
        const Vector2 relativeViewportSize{ viewportSizeX / textureSizeX, viewportSizeY / textureSizeY };
        const Vector2 relativeViewportOffset{ viewportOffsetX / textureSizeX, viewportOffsetY / textureSizeY };
        shaderParams_.shadowCubeUVBias_ =
            Vector2::ONE - 2.0f * cubeShadowMapPadding * shaderParams_.shadowMapInvSize_ / relativeViewportSize;
#ifdef URHO3D_OPENGL
        const Vector2 scale = relativeViewportSize * Vector2(1, -1);
        const Vector2 offset = Vector2(0, 1) + relativeViewportOffset * Vector2(1, -1);
#else
        const Vector2 scale = relativeViewportSize;
        const Vector2 offset = relativeViewportOffset;
#endif
        shaderParams_.shadowCubeAdjust_ = { scale, offset };
        break;
    }
    default:
        break;
    }

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

unsigned LightProcessor::RecalculatePipelineStateHash() const
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

void LightProcessor::CollectLitGeometriesAndMaybeShadowCasters(SceneLightProcessContext& ctx)
{
    Octree* octree = ctx.frameInfo_.octree_;
    switch (light_->GetLightType())
    {
    case LIGHT_SPOT:
    {
        SpotLightLitGeometriesQuery query(litGeometries_, hasShadow_ ? &tempShadowCasters_ : nullptr,
            ctx.dp_, light_, ctx.frameInfo_.camera_->GetViewMask());
        octree->GetDrawables(query);
        break;
    }
    case LIGHT_POINT:
    {
        PointLightLitGeometriesQuery query(litGeometries_, hasShadow_ ? &tempShadowCasters_ : nullptr,
            ctx.dp_, light_, ctx.frameInfo_.camera_->GetViewMask());
        octree->GetDrawables(query);
        break;
    }
    case LIGHT_DIRECTIONAL:
    {
        const unsigned lightMask = light_->GetLightMask();
        for (Drawable* drawable : ctx.dp_->GetVisibleGeometries())
        {
            if (drawable->GetLightMaskInZone() & lightMask)
                litGeometries_.push_back(drawable);
        };
        break;
    }
    }
}

Camera* LightProcessor::GetOrCreateShadowCamera(unsigned split)
{
    SharedPtr<Camera>& camera = splits_[split].shadowCamera_;
    if (!camera)
    {
        auto node = MakeShared<Node>(light_->GetContext());
        camera = node->CreateComponent<Camera>();
        splits_[split].shadowCameraNode_ = node;
        splits_[split].shadowCamera_ = camera;
    }
    camera->SetOrthographic(false);
    camera->SetZoom(1.0f);
    return splits_[split].shadowCamera_;
}

void LightProcessor::SetupShadowCameras(SceneLightProcessContext& ctx)
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
            splits_[i].SetupDirLightShadowCamera(light_, ctx.frameInfo_.camera_, litGeometries_, ctx.dp_);

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

void LightProcessor::ProcessShadowCasters(SceneLightProcessContext& ctx,
    const ea::vector<Drawable*>& drawables, unsigned splitIndex)
{
    auto& split = splits_[splitIndex];
    ctx.dp_->PreprocessShadowCasters(split.shadowCasters_,
        drawables, split.zRange_, light_, split.shadowCamera_);
}

IntVector2 LightProcessor::GetSplitsGridSize() const
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
