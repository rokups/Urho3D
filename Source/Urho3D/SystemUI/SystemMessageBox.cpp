//
// Copyright (c) 2017 the Urho3D project.
// Copyright (c) 2008-2015 the Urho3D project.
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

#include "Urho3D/Core/Context.h"
#include "Urho3D/Graphics/Graphics.h"
#include "Urho3D/IO/Log.h"
#include "SystemUIEvents.h"
#include "SystemUI.h"
#include "SystemMessageBox.h"

namespace Urho3D
{

SystemMessageBox::SystemMessageBox(Context* context, const String& messageString, const String& titleString) :
    Object(context),
    titleText_(0),
    messageText_(messageString),
    isOpen_(true)
{
    SetTitle(titleString);
    Graphics* graphics = GetSubsystem<Graphics>();
    windowSize_ = ImVec2(300, 150);
    windowPosition_ = ImVec2(graphics->GetWidth() / 2 - windowSize_.x / 2, graphics->GetHeight() / 2 - windowSize_.y / 2);
    SubscribeToEvent(E_SYSTEMUIFRAME, URHO3D_HANDLER(SystemMessageBox, RenderFrame));
}

SystemMessageBox::~SystemMessageBox()
{
    UnsubscribeFromAllEvents();
}

void SystemMessageBox::RegisterObject(Context* context)
{
    context->RegisterFactory<SystemMessageBox>();
}

void SystemMessageBox::SetTitle(const String& text)
{
    titleText_ = ToString("%s##%p", text.CString(), this);
}

void SystemMessageBox::SetMessage(const String& text)
{
    messageText_ = text;
}

const String& SystemMessageBox::GetTitle() const
{
    return titleText_;
}

const String& SystemMessageBox::GetMessage() const
{
    return messageText_;
}

void SystemMessageBox::RenderFrame(StringHash eventType, VariantMap& eventData)
{
    using namespace MessageACK;
    ImGui::SetNextWindowPos(windowPosition_, ImGuiCond_FirstUseEver);
    if (ImGui::Begin(titleText_.CString(), &isOpen_, windowSize_, -1, ImGuiWindowFlags_NoCollapse|
                     ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::Text(messageText_.CString());
        auto region = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPos(ImVec2(region.x - 100 + 20, region.y + 20));

        bool closeWindow = false;
        bool status = false;
        if (ImGui::Button("Ok"))
        {
            closeWindow = true;
            status = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || !isOpen_)
        {
            closeWindow = true;
            status = false;
        }

        if (closeWindow)
        {
            SendEvent(E_MESSAGEACK, P_OK, status);
            UnsubscribeFromAllEvents();
            isOpen_ = false;
        }
    }
    ImGui::End();
}

}
