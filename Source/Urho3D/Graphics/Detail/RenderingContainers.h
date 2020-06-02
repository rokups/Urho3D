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

#include "../../Math/NumericRange.h"

#include <EASTL/vector.h>

namespace Urho3D
{

/// Collection of drawables in the Scene.
using DrawableCollection = ea::vector<Drawable*>;

/// Collection of geometries in the Scene.
using GeometryCollection = ea::vector<Drawable*>;

/// Collection of lights in the Scene.
using LightCollection = ea::vector<Light*>;

/// Collection of geometries in the Scene. Can be used from multiple threads.
using ThreadedGeometryCollection = ThreadedVector<Drawable*>;

/// Collection of lights in the Scene. Can be used from multiple threads.
using ThreadedLightCollection = ThreadedVector<Light*>;

/// Min and max Z value of drawable(s).
using DrawableZRange = NumericRange<float>;

/// Min and max Z value of scene. Can be used from multiple threads.
class SceneZRange
{
public:
    /// Clear in the beginning of the frame.
    void Clear(unsigned numThreads)
    {
        threadRanges_.clear();
        threadRanges_.resize(numThreads);
        sceneRangeDirty_ = true;
    }

    /// Accumulate min and max Z value.
    void Accumulate(unsigned threadIndex, const DrawableZRange& range)
    {
        threadRanges_[threadIndex] |= range;
    }

    /// Get value.
    const DrawableZRange& Get()
    {
        if (sceneRangeDirty_)
        {
            sceneRangeDirty_ = false;
            sceneRange_ = {};
            for (const DrawableZRange& range : threadRanges_)
                sceneRange_ |= range;
        }
        return sceneRange_;
    }

private:
    /// Min and max Z value per thread.
    ea::vector<DrawableZRange> threadRanges_;
    /// Min and max Z value for Scene.
    DrawableZRange sceneRange_;
    /// Whether the Scene range is dirty.
    bool sceneRangeDirty_{};
};

/// Per-viewport drawable data, indexed via drawable index. Doesn't persist across frames.
struct TransientDrawableDataIndex
{
    /// Underlying type of traits.
    using TraitType = unsigned char;
    /// Traits.
    enum Trait : TraitType
    {
        /// Whether the drawable is updated.
        DrawableUpdated = 1 << 1,
        /// Whether the drawable has geometry visible from the main camera.
        DrawableVisibleGeometry = 1 << 2,
    };

    /// Traits.
    ea::vector<TraitType> traits_;
    /// Drawable min and max Z values. Invalid if drawable is not updated.
    ea::vector<DrawableZRange> zRange_;

    /// Reset cache in the beginning of the frame.
    void Reset(unsigned numDrawables)
    {
        traits_.resize(numDrawables);
        ea::fill(traits_.begin(), traits_.end(), TraitType{});

        zRange_.resize(numDrawables);
    }
};

}
