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

#include <Urho3D/Engine/Application.h>

namespace Urho3D
{

struct SampleInformation
{
    /// Title of the sample.
    ea::string name_;
    /// Type id of sample application.
    StringHash type_;
};

class SamplesManager : public Application
{
    // Enable type information.
    URHO3D_OBJECT(SamplesManager, Application);
public:
    /// Construct.
    explicit SamplesManager(Context* context);

    /// Setup before engine initialization. Modifies the engine parameters.
    void Setup() override;
    /// Setup after engine initialization. Creates the logo, console & debug HUD.
    void Start() override;
    /// Cleanup after the main loop. Called by Application.
    void Stop() override;

private:
    ///
    int GetSelectedIndex() const;
    ///
    UIElement* GetSampleButtonAt(int index);
    ///
    void OnClickSample(VariantMap& args);
    ///
    void OnKeyPress(VariantMap& args);
    ///
    void OnButtonPress(VariantMap& args);
    ///
    void OnFrameStart();
    /// Add a sample to sample list.
    template<typename T> void RegisterSample();
    /// Start execution of specified sample.
    void StartSample(StringHash sampleType);

    ///
    SharedPtr<Application> runningSample_;
    ///
    SharedPtr<UIElement> listViewHolder_;
    /// Logo sprite.
    SharedPtr<Sprite> logoSprite_;
    ///
    bool isClosing_ = false;
    /// Sample which will be started automatically.
    ea::string startSample_;

    /// Generic Serializable inspector.
    /// @{
    SharedPtr<Scene> inspectorNode_;
    bool oldMouseVisible_{};
    MouseMode oldMouseMode_{};
    /// @}
};

}
