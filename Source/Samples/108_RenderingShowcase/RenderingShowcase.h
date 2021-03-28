//
// Copyright (c) 2008-2020 the Urho3D project.
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

#include "Sample.h"

namespace Urho3D
{

class Drawable;
class Node;
class Scene;
class Zone;

}

/// Scene rendering showcase
class RenderingShowcase : public Sample
{
    URHO3D_OBJECT(RenderingShowcase, Sample);

public:
    /// Construct.
    explicit RenderingShowcase(Context* context);

    /// Setup after engine initialization and before running the main loop.
    void Start() override;

private:
    /// Setup shared scene objects.
    void CreateScene();
    /// Setup currently selected scene.
    void SetupSelectedScene();
    /// Set up a viewport for displaying the scene.
    void SetupViewport();
    /// Subscribe to application-wide logic update event.
    void SubscribeToEvents();
    /// Reads input and moves the camera.
    void MoveCamera(float timeStep);
    /// Handle the logic update event.
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    /// Construct an instruction text to the UI.
    void CreateInstructions();

    /// Scene that owns camera.
    SharedPtr<Scene> cameraScene_;
    /// Index of currently rendered scene.
    unsigned sceneIndex_{};
    /// List of all available scenes.
    ea::vector<ea::string> sceneNames_;
};
