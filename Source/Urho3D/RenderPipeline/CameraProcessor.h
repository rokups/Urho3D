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

#include "../Core/Object.h"
#include "../Graphics/Camera.h"
#include "../Graphics/GraphicsDefs.h"

#include <EASTL/span.h>

namespace Urho3D
{

struct FrameInfo;

/// Utility to process render camera (not cull camera!).
class URHO3D_API CameraProcessor : public Object
{
    URHO3D_OBJECT(CameraProcessor, Object);

public:
    explicit CameraProcessor(Context* context);
    void SetCameras(ea::span<Camera* const> cameras);

    unsigned GetPipelineStateHash() const;
    bool IsCameraReversed() const;
    bool IsCameraOrthographic() const { return isCameraOrthographic_; }
    bool IsCameraClipped() const { return isCameraClipped_; }
    FillMode GetCameraFillMode() const { return cameraFillMode_; }

    /// Callbacks from SceneProcessor
    /// @{
    void OnUpdateBegin(const FrameInfo& frameInfo);
    void OnRenderBegin(const FrameInfo& frameInfo);
    void OnRenderEnd(const FrameInfo& frameInfo);
    /// @}

private:
    void UpdateCamera(const FrameInfo& frameInfo, Camera* camera);

    bool isCameraOrthographic_{};
    bool isCameraFlippedByUser_{};
    bool isReflectionCamera_{};
    bool isCameraClipped_{};
    bool flipCameraForRendering_{};
    FillMode cameraFillMode_{};
    ea::vector<WeakPtr<Camera>> cameras_{};
};

}
