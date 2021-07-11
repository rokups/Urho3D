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

#include "../Math/Color.h"
#include "../Math/Vector3.h"
#include "../Resource/ImageCube.h"

#include <EASTL/vector.h>
#include <EASTL/shared_ptr.h>

namespace Urho3D
{

/// Scene background description.
struct BakedSceneBackground
{
    float intensity_{};
    Color color_;
    SharedPtr<ImageCube> image_;

    Vector3 SampleLinear(const Vector3& direction) const
    {
        return intensity_ * SampleGammaInternal(direction).GammaToLinear().ToVector3();
    }

private:
    Color SampleGammaInternal(const Vector3& direction) const
    {
        return image_ ? image_->SampleNearest(direction) : color_;
    }
};

/// Immutable array of scene backgrounds.
using BakedSceneBackgroundArrayPtr = ea::shared_ptr<const ea::vector<BakedSceneBackground>>;

}
