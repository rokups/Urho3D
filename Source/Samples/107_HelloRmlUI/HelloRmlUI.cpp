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
#include <Urho3D/Graphics/Texture2D.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/Technique.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Graphics/Texture2D.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/RmlUI/RmlUI.h>
#include <Urho3D/RmlUI/RmlCanvasComponent.h>
#include <RmlUi/Debugger.h>

#include "HelloRmlUI.h"

#include <Urho3D/DebugNew.h>


SimpleWindow::SimpleWindow(Context* context)
    : RmlUIComponent(context)
{
}

void SimpleWindow::OnNodeSet(Node* node)
{
    BaseClassName::OnNodeSet(node);

    RmlUI* ui = GetUI();
    Rml::Context* context = ui->GetRmlContext();
    if (node != nullptr && !model_)
    {
        // Create a data model for connecting UI with state kept in this class.
        // Important: there can only be one data model with given name per unique RmlUI subsystem!
        Rml::DataModelConstructor constructor = context->CreateDataModel("example_model");
        assert(constructor);
        constructor.Bind("slider_value", &sliderValue_);
        constructor.Bind("counter", &counter_);
        constructor.Bind("progress", &progress_);
        constructor.BindEventCallback("count", &SimpleWindow::CountClicks, this);
        model_ = constructor.GetModelHandle();

        // Load UI.
        SetResource("UI/HelloRmlUI.rml");
        SetOpen(true);

        // Act on pressing window close button.
        SubscribeToEvent(ui, "CloseWindow", &SimpleWindow::OnCloseWindow);
    }
    else if (node == nullptr && model_)
    {
        // Dispose of data model when it is no longer necessary.
        context->RemoveDataModel("example_model");
        model_ = nullptr;
    }
}

void SimpleWindow::CountClicks(Rml::DataModelHandle modelHandle, Rml::Event& ev, const Rml::VariantList& arguments)
{
    // Increase counter and notify model of it's update.
    counter_++;
    modelHandle.DirtyVariable("counter");
}

void SimpleWindow::Update(float timeStep)
{
    // Animate progressbars
    progress_ = (Sin(GetSubsystem<Time>()->GetElapsedTime() * 50) + 1) / 2;
    model_.DirtyVariable("progress");
}

void SimpleWindow::Reload()
{
    RmlUI* ui = GetSubsystem<RmlUI>();
    document_ = ui->ReloadDocument(document_);
    // Model does not have to be recreated and old model will be reused. State stored in the model persists across reloads.
}

void SimpleWindow::OnCloseWindow(StringHash, VariantMap& args)
{
    Rml::Element* element = static_cast<Rml::Element*>(args["_Element"].GetVoidPtr());
    if (element->GetOwnerDocument() == document_)
    {
        document_->Close();
        document_ = nullptr;
    }
}


HelloRmlUI::HelloRmlUI(Context* context)
    : Sample(context)
{
}

void HelloRmlUI::Start()
{
    // Register custom components.
    context_->RegisterFactory<SimpleWindow>();

    // Execute base class startup
    Sample::Start();

    // Initialize Scene
    InitScene();

    // Initialize Window
    InitWindow();

    // Set the mouse mode to use in the sample
    Sample::InitMouseMode(MM_FREE);
}

void HelloRmlUI::Stop()
{
    // Only necessary so sample can be reopened. Under normal circumnstances applications do not need to do this.
    context_->RemoveFactory<SimpleWindow>();
}

void HelloRmlUI::InitWindow()
{
    auto* ui = context_->GetSubsystem<RmlUI>();

    // Initialize fonts in backbuffer UI.
    // This code is moved to SamplesManager
    // ui->LoadFont("Fonts/NotoSans-Condensed.ttf", false);
    // ui->LoadFont("Fonts/NotoSans-CondensedBold.ttf", false);
    // ui->LoadFont("Fonts/NotoSans-CondensedBoldItalic.ttf", false);
    // ui->LoadFont("Fonts/NotoSans-CondensedItalic.ttf", false);

    // Create a window rendered into backbuffer.
    window_ = scene_->CreateComponent<SimpleWindow>();

    // Node that will get UI rendered on it.
    Node* boxNode = scene_->GetChild("Box");

    // Create a texture we will render to.
    texture_ = context_->CreateObject<Texture2D>();

    // Create a material that will display UI texture on a cube.
    material_ = context_->CreateObject<Material>();
    material_->SetTechnique(0, GetSubsystem<ResourceCache>()->GetResource<Technique>("Techniques/DiffUnlit.xml"));
    material_->SetTexture(TU_DIFFUSE, texture_);

    // Create a component that sets up UI rendering.
    auto* renderer = boxNode->CreateComponent<RmlCanvasComponent>();
    renderer->GetUI()->LoadFont("Fonts/NotoSans-Condensed.ttf", false);
    renderer->GetUI()->LoadFont("Fonts/NotoSans-CondensedBold.ttf", false);
    renderer->GetUI()->LoadFont("Fonts/NotoSans-CondensedBoldItalic.ttf", false);
    renderer->GetUI()->LoadFont("Fonts/NotoSans-CondensedItalic.ttf", false);
    renderer->SetTexture(texture_);
    renderer->SetUISize({512, 512});
    renderer->SetRemapMousePos(true);

    // Create a window that will be rendered on a cube.
    windowOnCube_ = boxNode->CreateComponent<SimpleWindow>();

    // Create a StaticModel for our cube.
    auto* model = boxNode->GetComponent<StaticModel>();

    // And set a material to it so UI would be rendered on to cube.
    model->SetMaterial(material_);

    // Subscribe to update event for handling keys and animating cube.
    SubscribeToEvent(E_UPDATE, &HelloRmlUI::OnUpdate);
}

void HelloRmlUI::InitScene()
{
    auto* cache = GetSubsystem<ResourceCache>();

    scene_ = new Scene(context_);
    scene_->CreateComponent<Octree>();
    auto* zone = scene_->CreateComponent<Zone>();
    zone->SetBoundingBox(BoundingBox(-1000.0f, 1000.0f));
    zone->SetFogColor(Color::GRAY);
    zone->SetFogStart(100.0f);
    zone->SetFogEnd(300.0f);

    // Create a child scene node (at world origin) and a StaticModel component into it.
    Node* boxNode = scene_->CreateChild("Box");
    boxNode->SetScale(Vector3(5.0f, 5.0f, 5.0f));
    boxNode->SetRotation(Quaternion(90, Vector3::LEFT));

    // Create a box model and hide it initially.
    auto* boxModel = boxNode->CreateComponent<StaticModel>();
    boxModel->SetModel(cache->GetResource<Model>("Models/Box.mdl"));

    // Create a camera.
    cameraNode_ = scene_->CreateChild("Camera");
    cameraNode_->CreateComponent<Camera>();

    // Set an initial position for the camera scene node.
    cameraNode_->SetPosition(Vector3(0.0f, 0.0f, -10.0f));

    // Set up a viewport so 3D scene can be visible.
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}

void HelloRmlUI::OnUpdate(StringHash, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    // Animate cube.
    Node* node = scene_->GetChild("Box");
    node->Yaw(6.0f * timeStep * 1.5f);
    node->Roll(-6.0f * timeStep * 1.5f);
    node->Pitch(-6.0f * timeStep * 1.5f);

    Input* input = GetSubsystem<Input>();
    if (input->GetKeyPress(KEY_F5))
    {
        window_->Reload();
        windowOnCube_->Reload();
    }

    if (input->GetKeyPress(KEY_F9))
    {
        auto* ui = context_->GetSubsystem<RmlUI>();
        ui->SetDebuggerVisible(!Rml::Debugger::IsVisible());
    }
}
