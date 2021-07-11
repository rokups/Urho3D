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
#include "../Graphics/Light.h"
#include "../RenderPipeline/DrawableProcessor.h"
#include "../RenderPipeline/LightProcessorQuery.h"
#include "../Scene/Node.h"

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

/// Return light sphere for the query.
Sphere GetLightSphere(Light* light)
{
    return Sphere(light->GetNode()->GetWorldPosition(), light->GetRange());
}

}

PointLightGeometryQuery::PointLightGeometryQuery(
    ea::vector<Drawable*>& result, bool& hasLitGeometries, ea::vector<Drawable*>* shadowCasters,
    const DrawableProcessor* drawableProcessor, Light* light, unsigned viewMask)
    : SphereOctreeQuery(result, GetLightSphere(light), DRAWABLE_GEOMETRY, viewMask)
    , hasLitGeometries_(hasLitGeometries)
    , shadowCasters_(shadowCasters)
    , drawableProcessor_(drawableProcessor)
    , lightMask_(light->GetLightMaskEffective())
{
    hasLitGeometries_ = false;
    if (shadowCasters_)
        shadowCasters_->clear();
}

void PointLightGeometryQuery::TestDrawables(Drawable** start, Drawable** end, bool inside)
{
    for (Drawable* drawable : MakeIteratorRange(start, end))
    {
        const auto result = IsLitOrShadowCaster(drawable, inside);
        if (result.isLit_)
            hasLitGeometries_ = true;
        if (result.isForwardLit_)
            result_.push_back(drawable);
        if (result.isShadowCaster_)
            shadowCasters_->push_back(drawable);
    }
}

LightGeometryQueryResult PointLightGeometryQuery::IsLitOrShadowCaster(Drawable* drawable, bool inside) const
{
    const unsigned drawableIndex = drawable->GetDrawableIndex();
    const unsigned geometryFlags = drawableProcessor_->GetGeometryRenderFlags(drawableIndex);

    const bool isInside = (drawable->GetDrawableFlags() & drawableFlags_)
        && (drawable->GetViewMask() & viewMask_)
        && (inside || sphere_.IsInsideFast(drawable->GetWorldBoundingBox()));
    const bool isLit = isInside
        && (geometryFlags & GeometryRenderFlag::Lit)
        && (drawable->GetLightMaskInZone() & lightMask_);
    const bool isForwardLit = isLit && (geometryFlags & GeometryRenderFlag::ForwardLit);
    const bool isShadowCaster = shadowCasters_ && isInside
        && drawable->GetCastShadows()
        && (drawable->GetShadowMask() & lightMask_);
    return { isLit, isForwardLit, isShadowCaster };
}

SpotLightGeometryQuery::SpotLightGeometryQuery(
    ea::vector<Drawable*>& result, bool& hasLitGeometries, ea::vector<Drawable*>* shadowCasters,
    const DrawableProcessor* drawableProcessor, Light* light, unsigned viewMask)
    : FrustumOctreeQuery(result, light->GetFrustum(), DRAWABLE_GEOMETRY, viewMask)
    , hasLitGeometries_(hasLitGeometries)
    , shadowCasters_(shadowCasters)
    , drawableProcessor_(drawableProcessor)
    , lightMask_(light->GetLightMaskEffective())
{
    hasLitGeometries_ = false;
    if (shadowCasters_)
        shadowCasters_->clear();
}

void SpotLightGeometryQuery::TestDrawables(Drawable** start, Drawable** end, bool inside)
{
    for (Drawable* drawable : MakeIteratorRange(start, end))
    {
        const auto result = IsLitOrShadowCaster(drawable, inside);
        if (result.isLit_)
            hasLitGeometries_ = true;
        if (result.isForwardLit_)
            result_.push_back(drawable);
        if (result.isShadowCaster_)
            shadowCasters_->push_back(drawable);
    }
}

LightGeometryQueryResult SpotLightGeometryQuery::IsLitOrShadowCaster(Drawable* drawable, bool inside) const
{
    const unsigned drawableIndex = drawable->GetDrawableIndex();
    const unsigned geometryFlags = drawableProcessor_->GetGeometryRenderFlags(drawableIndex);

    const bool isInside = (drawable->GetDrawableFlags() & drawableFlags_)
        && (drawable->GetViewMask() & viewMask_)
        && (inside || frustum_.IsInsideFast(drawable->GetWorldBoundingBox()));
    const bool isLit = isInside
        && (geometryFlags & GeometryRenderFlag::Lit)
        && (drawable->GetLightMaskInZone() & lightMask_);
    const bool isForwardLit = isLit && (geometryFlags & GeometryRenderFlag::ForwardLit);
    const bool isShadowCaster = shadowCasters_ && isInside
        && drawable->GetCastShadows()
        && (drawable->GetShadowMask() & lightMask_);
    return { isLit, isForwardLit, isShadowCaster };
}

DirectionalLightShadowCasterQuery::DirectionalLightShadowCasterQuery(ea::vector<Drawable*>& result,
    const Frustum& frustum, DrawableFlags drawableFlags, Light* light, unsigned viewMask)
    : FrustumOctreeQuery(result, frustum, drawableFlags, viewMask)
    , lightMask_(light->GetLightMask())
{
}

void DirectionalLightShadowCasterQuery::TestDrawables(Drawable** start, Drawable** end, bool inside)
{
    for (Drawable* drawable : MakeIteratorRange(start, end))
    {
        if (IsShadowCaster(drawable, inside))
            result_.push_back(drawable);
    }
}

bool DirectionalLightShadowCasterQuery::IsShadowCaster(Drawable* drawable, bool inside) const
{
    return drawable->GetCastShadows()
        && (drawable->GetDrawableFlags() & drawableFlags_)
        && (drawable->GetViewMask() & viewMask_)
        && (drawable->GetShadowMask() & lightMask_)
        && (inside || frustum_.IsInsideFast(drawable->GetWorldBoundingBox()));
}

}
