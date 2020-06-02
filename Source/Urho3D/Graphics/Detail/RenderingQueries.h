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

#include "../../Graphics/Drawable.h"
#include "../../Graphics/Light.h"
#include "../../Graphics/OctreeQuery.h"
#include "../../Graphics/Detail/RenderingContainers.h"
#include "../../Scene/Node.h"

#include <EASTL/vector.h>

namespace Urho3D
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
        const TransientDrawableDataIndex& transientData, Light* light)
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
            if (traits & TransientDrawableDataIndex::DrawableVisibleGeometry)
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
    const TransientDrawableDataIndex* transientData_{};
    /// Light mask to check.
    unsigned lightMask_{};
};

/// Frustum Query for spot light.
struct SpotLightLitGeometriesQuery : public FrustumOctreeQuery
{
    /// Construct.
    SpotLightLitGeometriesQuery(ea::vector<Drawable*>& result,
        const TransientDrawableDataIndex& transientData, Light* light)
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
            if (traits & TransientDrawableDataIndex::DrawableVisibleGeometry)
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
    const TransientDrawableDataIndex* transientData_{};
    /// Light mask to check.
    unsigned lightMask_{};
};


}
