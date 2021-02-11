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

#include "../Graphics/OctreeQuery.h"

#include <EASTL/vector.h>

namespace Urho3D
{

class DrawableProcessor;
class Light;

/// Result of light query for drawable.
struct LightGeometryQueryResult
{
    /// Whether the geometry is lit.
    bool isLit_{};
    /// Whether the geometry is shadow caster.
    bool isShadowCaster_{};
};

/// Frustum query for point light lit geometries and shadow casters.
class URHO3D_API PointLightGeometryQuery : public SphereOctreeQuery
{
public:
    /// Construct.
    PointLightGeometryQuery(ea::vector<Drawable*>& result, ea::vector<Drawable*>* shadowCasters,
        const DrawableProcessor* drawableProcessor, Light* light, unsigned viewMask);

    /// Intersection test for drawables.
    void TestDrawables(Drawable** start, Drawable** end, bool inside) override;

private:
    /// Return whether the drawable is lit and/or shadow caster.
    LightGeometryQueryResult IsLitOrShadowCaster(Drawable* drawable, bool inside) const;

    /// Result array of shadow casters, if applicable.
    ea::vector<Drawable*>* shadowCasters_{};
    /// Visiblity cache.
    const DrawableProcessor* drawableProcessor_{};
    /// Light mask to check.
    unsigned lightMask_{};
};

/// Frustum query for spot light lit geometries and shadow casters.
class URHO3D_API SpotLightGeometryQuery : public FrustumOctreeQuery
{
public:
    /// Construct.
    SpotLightGeometryQuery(ea::vector<Drawable*>& result, ea::vector<Drawable*>* shadowCasters,
        const DrawableProcessor* drawableProcessor, Light* light, unsigned viewMask);

    /// Intersection test for drawables.
    void TestDrawables(Drawable** start, Drawable** end, bool inside) override;

private:
    /// Return whether the drawable is lit and/or shadow caster.
    LightGeometryQueryResult IsLitOrShadowCaster(Drawable* drawable, bool inside) const;

    /// Result array of shadow casters, if applicable.
    ea::vector<Drawable*>* shadowCasters_{};
    /// Visiblity cache.
    const DrawableProcessor* drawableProcessor_{};
    /// Light mask to check.
    unsigned lightMask_{};
};

/// Frustum query for directional light shadow casters.
class URHO3D_API DirectionalLightShadowCasterQuery : public FrustumOctreeQuery
{
public:
    /// Construct with frustum and query parameters.
    DirectionalLightShadowCasterQuery(ea::vector<Drawable*>& result,
        const Frustum& frustum, DrawableFlags drawableFlags, Light* light, unsigned viewMask);

    /// Intersection test for drawables.
    void TestDrawables(Drawable** start, Drawable** end, bool inside) override;

private:
    /// Return whether the drawable is shadow caster.
    bool IsShadowCaster(Drawable* drawable, bool inside) const;

    /// Light mask to check.
    unsigned lightMask_{};
};

}
