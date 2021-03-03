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
    bool isLit_{};
    bool isForwardLit_{};
    bool isShadowCaster_{};
};

/// Frustum query for point light forward lit geometries and shadow casters.
class URHO3D_API PointLightGeometryQuery : public SphereOctreeQuery
{
public:
    PointLightGeometryQuery(ea::vector<Drawable*>& result, bool& hasLitGeometries,
        ea::vector<Drawable*>* shadowCasters,
        const DrawableProcessor* drawableProcessor, Light* light, unsigned viewMask);

    void TestDrawables(Drawable** start, Drawable** end, bool inside) override;

private:
    LightGeometryQueryResult IsLitOrShadowCaster(Drawable* drawable, bool inside) const;

    /// Immutable
    /// @{
    const DrawableProcessor* drawableProcessor_{};
    const unsigned lightMask_{};
    /// @}

    bool& hasLitGeometries_;
    /// Result array of shadow casters, if applicable.
    ea::vector<Drawable*>* shadowCasters_{};
};

/// Frustum query for spot light forward lit geometries and shadow casters.
class URHO3D_API SpotLightGeometryQuery : public FrustumOctreeQuery
{
public:
    SpotLightGeometryQuery(ea::vector<Drawable*>& result, bool& hasLitGeometries,
        ea::vector<Drawable*>* shadowCasters,
        const DrawableProcessor* drawableProcessor, Light* light, unsigned viewMask);

    void TestDrawables(Drawable** start, Drawable** end, bool inside) override;

private:
    LightGeometryQueryResult IsLitOrShadowCaster(Drawable* drawable, bool inside) const;

    /// Immutable
    /// @{
    const DrawableProcessor* drawableProcessor_{};
    const unsigned lightMask_{};
    /// @}

    bool& hasLitGeometries_;
    /// Result array of shadow casters, if applicable.
    ea::vector<Drawable*>* shadowCasters_{};
};

/// Frustum query for directional light shadow casters.
class URHO3D_API DirectionalLightShadowCasterQuery : public FrustumOctreeQuery
{
public:
    DirectionalLightShadowCasterQuery(ea::vector<Drawable*>& result,
        const Frustum& frustum, DrawableFlags drawableFlags, Light* light, unsigned viewMask);

    void TestDrawables(Drawable** start, Drawable** end, bool inside) override;

private:
    bool IsShadowCaster(Drawable* drawable, bool inside) const;

    const unsigned lightMask_{};
};

}
