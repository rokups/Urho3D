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

#include "../Core/Signal.h"
#include "../RenderPipeline/BatchStateCache.h"
#include "../RenderPipeline/LightProcessor.h"
#include "../Scene/Serializable.h"

namespace Urho3D
{

class DrawCommandQueue;
struct FrameInfo;

/// Base interface of render pipeline required by Render Pipeline classes.
class URHO3D_API RenderPipelineInterface
    : public Serializable
    , public BatchStateCacheCallback
{
    URHO3D_OBJECT(RenderPipelineInterface, Serializable);

public:
    using Serializable::Serializable;

    /// Return default draw queue that can be reused.
    virtual DrawCommandQueue* GetDefaultDrawQueue() = 0;

    /// Callbacks
    /// @{
    Signal<void(const FrameInfo& frameInfo)> OnUpdateBegin;
    Signal<void(const FrameInfo& frameInfo)> OnUpdateEnd;
    Signal<void(const FrameInfo& frameInfo)> OnRenderBegin;
    Signal<void(const FrameInfo& frameInfo)> OnRenderEnd;
    Signal<void()> OnPipelineStatesInvalidated;
    /// @}
};

}
