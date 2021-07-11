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

#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Graphics/RenderPath.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/Button.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Slider.h>
#include <Urho3D/UI/UI.h>
#include <Urho3D/UI/UIEvents.h>
#include <Urho3D/UI/Text.h>

#include "RenderingShowcase.h"

#include <Urho3D/DebugNew.h>


RenderingShowcase::RenderingShowcase(Context* context) : Sample(context)
{
    // All these scenes correspond to Scenes/RenderingShowcase_*.xml resources
    sceneNames_.push_back({ "0" });
    sceneNames_.push_back({ "2_Dynamic", "2_BakedDirect", "2_BakedIndirect", "2_BakedDirectIndirect" });
    // Keep 1 last because it may crash mobile browsers
    sceneNames_.push_back({ "1" });
}

void RenderingShowcase::Start()
{
    // Execute base class startup
    Sample::Start();

    // Create scene content
    CreateScene();
    SetupSelectedScene();

    // Create the UI content
    CreateInstructions();

    // Setup the viewport for displaying the scene
    SetupViewport();

    // Subscribe to global events for camera movement
    SubscribeToEvents();

    // Set the mouse mode to use in the sample
    Sample::InitMouseMode(MM_RELATIVE);
}

void RenderingShowcase::CreateInstructions()
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* ui = GetSubsystem<UI>();

    // Construct new Text object, set string to display and font to use
    auto* instructionText = ui->GetRoot()->CreateChild<Text>();
    instructionText->SetText("Press Tab to switch scene. Press Q to switch scene mode. \n"
        "Press F to toggle probe object. Use WASD keys and mouse to move.");
    instructionText->SetFont(cache->GetResource<Font>("Fonts/Anonymous Pro.ttf"), 15);
    instructionText->SetTextAlignment(HA_CENTER);

    // Position the text relative to the screen center
    instructionText->SetHorizontalAlignment(HA_CENTER);
    instructionText->SetVerticalAlignment(VA_CENTER);
    instructionText->SetPosition(0, ui->GetRoot()->GetHeight() / 4);
}

void RenderingShowcase::CreateScene()
{
    auto* cache = GetSubsystem<ResourceCache>();

    scene_ = MakeShared<Scene>(context_);
    cameraScene_ = MakeShared<Scene>(context_);

    // Create the camera (not included in the scene file)
    cameraNode_ = cameraScene_->CreateChild("Camera");
    cameraNode_->CreateComponent<Camera>();

    Node* probeObjectNode = cameraNode_->CreateChild();
    probeObjectNode->SetPosition({ 0.0f, 0.0f, 1.0f });
    probeObjectNode->SetScale(0.5f);

    probeObject_ = probeObjectNode->CreateComponent<StaticModel>();
    probeObject_->SetModel(cache->GetResource<Model>("Models/TeaPot.mdl"));
    probeObject_->SetCastShadows(true);
    probeObject_->SetGlobalIlluminationType(GlobalIlluminationType::BlendLightProbes);
}

void RenderingShowcase::SetupSelectedScene(bool resetCamera)
{
    auto* cache = GetSubsystem<ResourceCache>();

    const bool isProbeObjectVisible = probeObject_->IsInOctree();

    if (isProbeObjectVisible)
        scene_->GetComponent<Octree>()->RemoveManualDrawable(probeObject_);

    // Load scene content prepared in the editor (XML format). GetFile() returns an open file from the resource system
    // which scene.LoadXML() will read
    const ea::string fileName = Format("Scenes/RenderingShowcase_{}.xml", sceneNames_[sceneIndex_][sceneMode_]);
    SharedPtr<File> file = cache->GetFile(fileName);
    scene_->LoadXML(*file);

    if (resetCamera)
    {
        cameraNode_->SetPosition({ 0.0f, 4.0f, 8.0f });
        cameraNode_->LookAt(Vector3::ZERO);

        yaw_ = cameraNode_->GetRotation().YawAngle();
        pitch_ = cameraNode_->GetRotation().PitchAngle();
    }

    if (isProbeObjectVisible)
        scene_->GetComponent<Octree>()->AddManualDrawable(probeObject_);
}

void RenderingShowcase::SetupViewport()
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* renderer = GetSubsystem<Renderer>();

    // Set up a viewport to the Renderer subsystem so that the 3D scene can be seen
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void RenderingShowcase::SubscribeToEvents()
{
    // Subscribe HandleUpdate() function for camera motion
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(RenderingShowcase, HandleUpdate));
}

void RenderingShowcase::MoveCamera(float timeStep)
{
    // Right mouse button controls mouse cursor visibility: hide when pressed
    auto* input = GetSubsystem<Input>();

    // Movement speed as world units per second
    const float MOVE_SPEED = 10.0f;
    // Mouse sensitivity as degrees per pixel
    const float MOUSE_SENSITIVITY = 0.1f;

    // Use this frame's mouse motion to adjust camera node yaw and pitch. Clamp the pitch between -90 and 90 degrees
    IntVector2 mouseMove = input->GetMouseMove();
    yaw_ += MOUSE_SENSITIVITY * mouseMove.x_;
    pitch_ += MOUSE_SENSITIVITY * mouseMove.y_;
    pitch_ = Clamp(pitch_, -90.0f, 90.0f);

    // Construct new orientation for the camera scene node from yaw and pitch. Roll is fixed to zero
    cameraNode_->SetRotation(Quaternion(pitch_, yaw_, 0.0f));

    // Read WASD keys and move the camera scene node to the corresponding direction if they are pressed
    if (input->GetKeyDown(KEY_W))
        cameraNode_->Translate(Vector3::FORWARD * MOVE_SPEED * timeStep);
    if (input->GetKeyDown(KEY_S))
        cameraNode_->Translate(Vector3::BACK * MOVE_SPEED * timeStep);
    if (input->GetKeyDown(KEY_A))
        cameraNode_->Translate(Vector3::LEFT * MOVE_SPEED * timeStep);
    if (input->GetKeyDown(KEY_D))
        cameraNode_->Translate(Vector3::RIGHT * MOVE_SPEED * timeStep);
}

void RenderingShowcase::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;

    // Take the frame time step, which is stored as a float
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    // Move the camera, scale movement with time step
    MoveCamera(timeStep);

    // Keep probe object orientation
    probeObject_->GetNode()->SetWorldRotation(Quaternion::IDENTITY);

    // Update scene
    auto* input = GetSubsystem<Input>();
    if (sceneNames_.size() > 1 && input->GetKeyPress(KEY_TAB))
    {
        sceneIndex_ = (sceneIndex_ + 1) % sceneNames_.size();
        sceneMode_ = 0;
        SetupSelectedScene();
    }

    // Update scene mode
    if (sceneNames_[sceneIndex_].size() > 1 && input->GetKeyPress(KEY_Q))
    {
        sceneMode_ = (sceneMode_ + 1) % sceneNames_[sceneIndex_].size();
        SetupSelectedScene(false);
    }

    // Update probe object
    if (input->GetKeyPress(KEY_F))
    {
        const bool isProbeObjectVisible = probeObject_->IsInOctree();
        auto* octree = scene_->GetComponent<Octree>();
        if (isProbeObjectVisible)
            octree->RemoveManualDrawable(probeObject_);
        else
            octree->AddManualDrawable(probeObject_);
    }
}
