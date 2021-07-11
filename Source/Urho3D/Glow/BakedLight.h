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

/// \file

#pragma once

#include "../Graphics/Light.h"
#include "../Math/Vector3.h"
#include "../Scene/Node.h"

namespace Urho3D
{

/// Baked light description.
struct BakedLight
{
    /// Construct default.
    BakedLight() = default;

    /// Construct valid.
    explicit BakedLight(Light* light)
        : lightType_(light->GetLightType())
        , lightMode_(light->GetLightMode())
        , lightMask_(light->GetLightMask())
        , color_(light->GetEffectiveColor().GammaToLinear())
        , indirectBrightness_(light->GetIndirectBrightness())
        , distance_(light->GetRange())
        , fov_(light->GetFov())
        , cutoff_(Cos(fov_ * 0.5f))
        , radius_(lightType_ != LIGHT_DIRECTIONAL ? light->GetRadius() : 0.0f)
        , angle_(lightType_ == LIGHT_DIRECTIONAL ? light->GetRadius() : 0.0f)
        , halfAngleTan_(Tan(angle_ / 2.0f))
        , position_(light->GetNode()->GetWorldPosition())
        , rotation_(light->GetNode()->GetWorldRotation())
        , direction_(light->GetNode()->GetWorldDirection())
    {
    }

    /// Light type.
    LightType lightType_{};
    /// Light mode.
    LightMode lightMode_{};
    unsigned lightMask_{};
    /// Light color.
    Color color_{};
    /// Indirect brightness.
    float indirectBrightness_{};
    /// FOV angle (for spot lights).
    float fov_{};
    /// Cutoff aka Cos(FOV * 0.5) (for spot lights).
    float cutoff_{};
    /// Light distance (for spot and point lights).
    float distance_{};
    /// Light radius (for spot and point lights).
    float radius_{};
    /// Light angle (for directional light).
    float angle_{};
    /// Tangent of half light angle.
    float halfAngleTan_{};
    /// Position.
    Vector3 position_;
    /// Direction.
    Vector3 direction_;
    /// Rotation.
    Quaternion rotation_;
};

}
