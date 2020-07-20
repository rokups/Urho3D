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

#include "../Core/IteratorRange.h"
#include "../Math/Polyhedron.h"
#include "../Graphics/Camera.h"
#include "../Graphics/SceneLight.h"
#include "../Graphics/Octree.h"
#include "../Graphics/OctreeQuery.h"
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
    PointLightLitGeometriesQuery(ea::vector<Drawable*>& result,
        const SceneDrawableData& transientData, Light* light)
        : SphereOctreeQuery(result, GetLightSphere(light), DRAWABLE_GEOMETRY)
        , transientData_(&transientData)
        , lightMask_(light->GetLightMaskEffective())
    {
    }

    void TestDrawables(Drawable** start, Drawable** end, bool inside) override
    {
        for (Drawable* drawable : MakeIteratorRange(start, end))
        {
            const unsigned drawableIndex = drawable->GetDrawableIndex();
            const unsigned traits = transientData_->traits_[drawableIndex];
            if (traits & SceneDrawableData::DrawableVisibleGeometry)
            {
                if (drawable->GetLightMask() & lightMask_)
                {
                    if (inside || sphere_.IsInsideFast(drawable->GetWorldBoundingBox()))
                        result_.push_back(drawable);
                }
            }
        }
    }

    /// Visiblity cache.
    const SceneDrawableData* transientData_{};
    /// Light mask to check.
    unsigned lightMask_{};
};

/// Frustum Query for spot light.
struct SpotLightLitGeometriesQuery : public FrustumOctreeQuery
{
    /// Construct.
    SpotLightLitGeometriesQuery(ea::vector<Drawable*>& result,
        const SceneDrawableData& transientData, Light* light)
        : FrustumOctreeQuery(result, light->GetFrustum(), DRAWABLE_GEOMETRY)
        , transientData_(&transientData)
        , lightMask_(light->GetLightMaskEffective())
    {
    }

    void TestDrawables(Drawable** start, Drawable** end, bool inside) override
    {
        for (Drawable* drawable : MakeIteratorRange(start, end))
        {
            const unsigned drawableIndex = drawable->GetDrawableIndex();
            const unsigned traits = transientData_->traits_[drawableIndex];
            if (traits & SceneDrawableData::DrawableVisibleGeometry)
            {
                if (drawable->GetLightMask() & lightMask_)
                {
                    if (inside || frustum_.IsInsideFast(drawable->GetWorldBoundingBox()))
                        result_.push_back(drawable);
                }
            }
        }
    }

    /// Visiblity cache.
    const SceneDrawableData* transientData_{};
    /// Light mask to check.
    unsigned lightMask_{};
};

}

void SceneLight::BeginFrame(bool hasShadow)
{
    litGeometries_.clear();
    hasShadow_ = hasShadow && light_->GetCastShadows();
    MarkPipelineStateHashDirty();
}

void SceneLight::Process(Octree* octree, Camera* cullCamera, const DrawableZRange& sceneZRange,
    const ThreadedVector<Drawable*>& visibleGeometries, SceneDrawableData& drawableData)
{
    // Update lit geometries
    switch (light_->GetLightType())
    {
    case LIGHT_SPOT:
    {
        SpotLightLitGeometriesQuery query(litGeometries_, drawableData, light_);
        octree->GetDrawables(query);
        break;
    }
    case LIGHT_POINT:
    {
        PointLightLitGeometriesQuery query(litGeometries_, drawableData, light_);
        octree->GetDrawables(query);
        break;
    }
    case LIGHT_DIRECTIONAL:
    {
        const unsigned lightMask = light_->GetLightMask();
        visibleGeometries.ForEach([&](unsigned, unsigned, Drawable* drawable)
        {
            if (drawable->GetLightMask() & lightMask)
                litGeometries_.push_back(drawable);
        });
        break;
    }
    }

    if (hasShadow_)
    {
        SetupShadowCameras(cullCamera, sceneZRange, visibleGeometries, drawableData);
    }
}

unsigned SceneLight::RecalculatePipelineStateHash() const
{
    // TODO(renderer): Extract into pipeline state factory
    unsigned hash = 0;
    hash |= light_->GetLightType() & 0x3;
    hash |= static_cast<unsigned>(hasShadow_) << 2;
    hash |= static_cast<unsigned>(!!light_->GetShapeTexture()) << 3;
    hash |= static_cast<unsigned>(light_->GetSpecularIntensity() > 0.0f) << 4;
    hash |= static_cast<unsigned>(light_->GetShadowBias().normalOffset_ > 0.0f) << 5;
    return hash;
}

Camera* SceneLight::GetOrCreateShadowCamera(unsigned split)
{
    if (!shadowCameras_[split])
    {
        auto node = MakeShared<Node>(light_->GetContext());
        auto camera = node->CreateComponent<Camera>();
        camera->SetOrthographic(false);
        camera->SetZoom(1.0f);
        shadowCameraNodes_[split] = node;
        shadowCameras_[split] = camera;
    }
    return shadowCameras_[split];
}

void SceneLight::SetupShadowCameras(Camera* cullCamera, const DrawableZRange& sceneZRange,
    const ThreadedVector<Drawable*>& visibleGeometries, SceneDrawableData& drawableData)
{
    numSplits_ = 0;

    switch (light_->GetLightType())
    {
    case LIGHT_DIRECTIONAL:
    {
        const CascadeParameters& cascade = light_->GetShadowCascade();

        float nearSplit = cullCamera->GetNearClip();
        float farSplit;
        int numSplits = light_->GetNumShadowSplits();

        while (numSplits_ < numSplits)
        {
            // If split is completely beyond camera far clip, we are done
            if (nearSplit > cullCamera->GetFarClip())
                break;

            farSplit = Min(cullCamera->GetFarClip(), cascade.splits_[numSplits_]);
            if (farSplit <= nearSplit)
                break;

            // Setup the shadow camera for the split
            Camera* shadowCamera = GetOrCreateShadowCamera(numSplits_);
            shadowNearSplits_[numSplits_] = nearSplit;
            shadowFarSplits_[numSplits_] = farSplit;
            SetupDirLightShadowCamera(cullCamera, shadowCamera, light_, nearSplit, farSplit,
                sceneZRange, visibleGeometries, drawableData);

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

void SceneLight::SetupDirLightShadowCamera(Camera* cullCamera, Camera* shadowCamera,
    Light* light, float nearSplit, float farSplit, const DrawableZRange& sceneZRange,
    const ThreadedVector<Drawable*>& visibleGeometries, SceneDrawableData& drawableData)
{
    Node* shadowCameraNode = shadowCamera->GetNode();
    Node* lightNode = light->GetNode();
    float extrusionDistance = Min(cullCamera->GetFarClip(), light->GetShadowMaxExtrusion());
    const FocusParameters& parameters = light->GetShadowFocus();

    // Calculate initial position & rotation
    Vector3 pos = cullCamera->GetNode()->GetWorldPosition() - extrusionDistance * lightNode->GetWorldDirection();
    shadowCameraNode->SetTransform(pos, lightNode->GetWorldRotation());

    // Calculate main camera shadowed frustum in light's view space
    farSplit = Min(farSplit, cullCamera->GetFarClip());
    // Use the scene Z bounds to limit frustum size if applicable
    if (parameters.focus_)
    {
        nearSplit = Max(sceneZRange.first, nearSplit);
        farSplit = Min(sceneZRange.second, farSplit);
    }

    Frustum splitFrustum = cullCamera->GetSplitFrustum(nearSplit, farSplit);
    Polyhedron frustumVolume;
    frustumVolume.Define(splitFrustum);
    // If focusing enabled, clip the frustum volume by the combined bounding box of the lit geometries within the frustum
    if (parameters.focus_)
    {
        BoundingBox litGeometriesBox;
        unsigned lightMask = light->GetLightMaskEffective();

        for (Drawable* drawable : litGeometries_)
        {
            const DrawableZRange& drawableZRange = drawableData.zRange_[drawable->GetDrawableIndex()];
            if (drawableZRange.first <= farSplit && drawableZRange.second >= nearSplit)
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
    const Matrix3x4& lightView = shadowCamera->GetView();
    frustumVolume.Transform(lightView);

    // Fit the frustum volume inside a bounding box. If uniform size, use a sphere instead
    BoundingBox shadowBox;
    if (!parameters.nonUniform_)
        shadowBox.Define(Sphere(frustumVolume));
    else
        shadowBox.Define(frustumVolume);

    shadowCamera->SetOrthographic(true);
    shadowCamera->SetAspectRatio(1.0f);
    shadowCamera->SetNearClip(0.0f);
    shadowCamera->SetFarClip(shadowBox.max_.z_);

    // Center shadow camera on the bounding box. Can not snap to texels yet as the shadow map viewport is unknown
    QuantizeDirLightShadowCamera(shadowCamera, light, IntRect(0, 0, 0, 0), shadowBox);
}

void SceneLight::QuantizeDirLightShadowCamera(Camera* shadowCamera, Light* light, const IntRect& shadowViewport,
    const BoundingBox& viewBox)
{
    Node* shadowCameraNode = shadowCamera->GetNode();
    const FocusParameters& parameters = light->GetShadowFocus();
    auto shadowMapWidth = (float)(shadowViewport.Width());

    float minX = viewBox.min_.x_;
    float minY = viewBox.min_.y_;
    float maxX = viewBox.max_.x_;
    float maxY = viewBox.max_.y_;

    Vector2 center((minX + maxX) * 0.5f, (minY + maxY) * 0.5f);
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

    shadowCamera->SetOrthoSize(viewSize);

    // Center shadow camera to the view space bounding box
    Quaternion rot(shadowCameraNode->GetWorldRotation());
    Vector3 adjust(center.x_, center.y_, 0.0f);
    shadowCameraNode->Translate(rot * adjust, TS_WORLD);

    // If the shadow map viewport is known, snap to whole texels
    if (shadowMapWidth > 0.0f)
    {
        Vector3 viewPos(rot.Inverse() * shadowCameraNode->GetWorldPosition());
        // Take into account that shadow map border will not be used
        float invActualSize = 1.0f / (shadowMapWidth - 2.0f);
        Vector2 texelSize(viewSize.x_ * invActualSize, viewSize.y_ * invActualSize);
        Vector3 snap(-fmodf(viewPos.x_, texelSize.x_), -fmodf(viewPos.y_, texelSize.y_), 0.0f);
        shadowCameraNode->Translate(rot * snap, TS_WORLD);
    }
}

}
