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
#include "../RenderPipeline/LightProcessorQuery.h"
#include "../Scene/Node.h"

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

/// Cube shadow map padding, in pixels.
const float cubeShadowMapPadding = 2.0f;

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

LightProcessor::LightProcessor(Light* light, DrawableProcessor* drawableProcessor)
    : light_(light)
    , drawableProcessor_(drawableProcessor)
{
    //for (ShadowSplitProcessor& split : splits_)
    //    split.sceneLight_ = this;
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
                if (!sceneZRange.Interset(splits_[i].splitRange_))
                    continue;

                // Reuse lit geometry query for all except directional lights
                DirectionalLightShadowCasterQuery query(
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

    const auto hasShadowCaster = [](const ShadowSplitProcessor& split) { return !split.shadowCasters_.empty(); };
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
        ShadowSplitProcessor& split = splits_[splitIndex];
        split.Finalize(shadowMap.GetSplit(splitIndex, GetSplitsGridSize()));
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
        shaderParams_.shadowSplits_.x_ = splits_[0].splitRange_.second / cullCamera->GetFarClip();
    if (numSplits_ > 2)
        shaderParams_.shadowSplits_.y_ = splits_[1].splitRange_.second / cullCamera->GetFarClip();
    if (numSplits_ > 3)
        shaderParams_.shadowSplits_.z_ = splits_[2].splitRange_.second / cullCamera->GetFarClip();

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

void LightProcessor::SetNumSplits(unsigned numSplits)
{
    while (splits_.size() > numSplits)
        splits_.pop_back();
    while (splits_.size() < numSplits)
        splits_.emplace_back(this);
}

void LightProcessor::CollectLitGeometriesAndMaybeShadowCasters(SceneLightProcessContext& ctx)
{
    Octree* octree = ctx.frameInfo_.octree_;
    switch (light_->GetLightType())
    {
    case LIGHT_SPOT:
    {
        SpotLightGeometryQuery query(litGeometries_, hasShadow_ ? &tempShadowCasters_ : nullptr,
            ctx.dp_, light_, ctx.frameInfo_.camera_->GetViewMask());
        octree->GetDrawables(query);
        break;
    }
    case LIGHT_POINT:
    {
        PointLightGeometryQuery query(litGeometries_, hasShadow_ ? &tempShadowCasters_ : nullptr,
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

        SetNumSplits(numSplits);

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
            //Camera* shadowCamera = GetOrCreateShadowCamera(i);
            splits_[i].InitializeDirectional(ctx.frameInfo_.camera_, { nearSplit, farSplit }, litGeometries_);
            //splits_[i].zRange_ = { nearSplit, farSplit };
            //splits_[i].SetupDirLightShadowCamera(light_, ctx.frameInfo_.camera_, litGeometries_, ctx.dp_);

            nearSplit = farSplit;
            ++numSplits_;
        }
        break;
    }
    case LIGHT_SPOT:
    {

        SetNumSplits(1);
        splits_[0].InitializeSpot();

        numSplits_ = 1;
        break;
    }
    case LIGHT_POINT:
    {
        SetNumSplits(MAX_CUBEMAP_FACES);

        for (unsigned i = 0; i < MAX_CUBEMAP_FACES; ++i)
            splits_[i].InitializePoint(static_cast<CubeMapFace>(i));

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
        drawables, split.splitRange_, light_, split.shadowCamera_);
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
