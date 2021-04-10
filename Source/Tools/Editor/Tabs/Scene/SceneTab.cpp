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

#include <EASTL/sort.h>

#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Graphics/BillboardSet.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/DebugRenderer.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/GraphicsImpl.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Graphics/RenderPath.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Graphics/AnimatedModel.h>
#include <Urho3D/Graphics/Terrain.h>
#include <Urho3D/IO/ArchiveSerialization.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/SceneEvents.h>
#include <Urho3D/Scene/SceneManager.h>
#include <Urho3D/Scene/ObjectAnimation.h>
#include <Urho3D/Scene/ValueAnimation.h>
#include <Urho3D/SystemUI/DebugHud.h>

#include <IconFontCppHeaders/IconsFontAwesome5.h>
#include <Toolbox/Scene/DebugCameraController.h>
#include <Toolbox/SystemUI/Widgets.h>
#include <ImGui/imgui_internal.h>
#include <ImGuizmo/ImGuizmo.h>

#include "SceneTab.h"
#include "EditorEvents.h"
#include "Editor.h"
#include "Widgets.h"
#include "EditorSceneSettings.h"
#include "Tabs/HierarchyTab.h"
#include "Tabs/InspectorTab.h"
#include "Tabs/PreviewTab.h"


namespace Urho3D
{

static const IntVector2 cameraPreviewSize{320, 200};
static Matrix4 inversionMatrix(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1);

unsigned GetViewportTextureFormat()
{
#ifdef URHO3D_D3D11
    // DX11 doesn't have RGB texture format and we don't want ImGUI to use alpha
    return DXGI_FORMAT_B8G8R8X8_UNORM;
#else
    return Graphics::GetRGBFormat();
#endif
}

SceneTab::SceneTab(Context* context)
    : BaseClassName(context)
    , rect_({0, 0, 1024, 768})
    , gizmo_(context)
    , clipboard_(context)
{
    SetID("be1c7280-08e4-4b9f-b6ec-6d32bd9e3293");
    SetTitle("Scene");
    windowFlags_ = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    isUtility_ = true;
    noContentPadding_ = true;

    // Main viewport
    viewport_ = new Viewport(context_);
    viewport_->SetRect(rect_);
    texture_ = new Texture2D(context_);
    texture_->SetSize(rect_.Width(), rect_.Height(), GetViewportTextureFormat(), TEXTURE_RENDERTARGET);
    texture_->GetRenderSurface()->SetUpdateMode(SURFACE_UPDATEALWAYS);
    texture_->GetRenderSurface()->SetViewport(0, viewport_);

    // Camera preview objects
    cameraPreviewViewport_ = new Viewport(context_);
    cameraPreviewViewport_->SetRect(IntRect{{0, 0}, cameraPreviewSize});
    cameraPreviewViewport_->SetDrawDebug(false);
    cameraPreviewTexture_ = new Texture2D(context_);
    cameraPreviewTexture_->SetSize(cameraPreviewSize.x_, cameraPreviewSize.y_, GetViewportTextureFormat(), TEXTURE_RENDERTARGET);
    cameraPreviewTexture_->GetRenderSurface()->SetUpdateMode(SURFACE_UPDATEALWAYS);
    cameraPreviewTexture_->GetRenderSurface()->SetViewport(0, cameraPreviewViewport_);

    // Events
    SubscribeToEvent(E_UPDATE, [this](StringHash, VariantMap& args) { OnUpdate(args); });
    SubscribeToEvent(E_POSTRENDERUPDATE, [&](StringHash, VariantMap&) { RenderDebugInfo(); });
    SubscribeToEvent(E_ENDFRAME, [this](StringHash, VariantMap&) { UpdateCameras(); });
    SubscribeToEvent(E_SCENEACTIVATED, [this](StringHash, VariantMap& args) { OnSceneActivated(args); });
    SubscribeToEvent(E_EDITORPROJECTCLOSING, [this](StringHash, VariantMap&) { OnEditorProjectClosing(); });

    undo_->Connect(&gizmo_, this);

    UpdateUniqueTitle();
}

SceneTab::~SceneTab()
{
    Close();
    context_->RegisterSubsystem(new UI(context_));
}

void SceneTab::OnRenderContextMenu()
{
    BaseResourceTab::OnRenderContextMenu();
}

bool SceneTab::RenderWindowContent()
{
    ImGuiContext& g = *GImGui;
    bool open = true;

    if (GetScene() == nullptr)
        return true;

    // Focus window when appearing
    if (!isRendered_)
        ui::SetWindowFocus();

    if (!ui::BeginChild("Scene view", g.CurrentWindow->ContentRegionRect.GetSize(), false, windowFlags_))
    {
        ui::EndChild();
        return open;
    }

    ImGuiWindow* window = g.CurrentWindow;
    ImGuiViewport* viewport = window->Viewport;

#if 0
    const float dpi = viewport->DpiScale;
#else
    const float dpi = 1.0f;
#endif
    ImRect rect = ImRound(window->ContentRegionRect);
    IntVector2 textureSize{
        static_cast<int>(IM_ROUND(rect.GetWidth() * dpi)),
        static_cast<int>(IM_ROUND(rect.GetHeight() * dpi))
    };
    if (textureSize.x_ != texture_->GetWidth() || textureSize.y_ != texture_->GetHeight())
    {
        viewport_->SetRect(IntRect(IntVector2::ZERO, textureSize));
        texture_->SetSize(textureSize.x_, textureSize.y_, GetViewportTextureFormat(), TEXTURE_RENDERTARGET);
        texture_->GetRenderSurface()->SetViewport(0, viewport_);
        texture_->GetRenderSurface()->SetUpdateMode(SURFACE_UPDATEALWAYS);
    }

    bool wasActive = isViewportActive_;

    // Buttons must render above the viewport, but they also should be rendered first in order to take precedence in
    // item activation.
    viewportSplitter_.Split(window->DrawList, 3);
    viewportSplitter_.SetCurrentChannel(window->DrawList, 2);
    RenderToolbarButtons();
    viewportSplitter_.SetCurrentChannel(window->DrawList, 1);
    gizmo_.Manipulate(GetCamera(), selectedNodes_);
    viewportSplitter_.SetCurrentChannel(window->DrawList, 0);
    ui::SetCursorPos({0, 0});
    ui::ImageItem(texture_, rect.GetSize());
    isViewportActive_ = ui::ItemMouseActivation(MOUSEB_RIGHT) && ui::IsMouseDragging(MOUSEB_RIGHT);

    // Left click happens immediately.
    isClickedLeft_ = ui::IsItemClicked(MOUSEB_LEFT) && !ui::IsMouseDragging(MOUSEB_LEFT);
    // Right click is registerd only on release, because we want to support both opening a context menu and camera
    // rotation in scene viewport. Context menu is displayed on button release without dragging and camera is rotated
    // when mouse is dragged before button release.
    isClickedRight_ = ui::IsItemHovered() && ui::IsMouseReleased(MOUSEB_RIGHT) && !ui::IsMouseDragging(MOUSEB_RIGHT) &&
                      mouseButtonClickedViewport_ == ImGuiMouseButton_Right;   // Ensure that clicks from outside if this viewport do not register.

    // Store mouse button that clicked viewport.
    if (!ui::IsAnyMouseDown())
        mouseButtonClickedViewport_ = ImGuiMouseButton_COUNT;
    else
    {
        for (int i = 0; i < ImGuiMouseButton_COUNT; i++)
        {
            if (ui::IsMouseClicked(i))
            {
                mouseButtonClickedViewport_ = i;
                break;
            }
        }
    }
    ImRect viewportRect{ui::GetItemRectMin(), ui::GetItemRectMax()};
    viewportSplitter_.Merge(window->DrawList);

    RenderViewManipulator(rect);

    if (wasActive != isViewportActive_)
        GetSubsystem<SystemUI>()->SetMouseWrapping(isViewportActive_, true);

    // Render camera preview
    if (cameraPreviewViewport_->GetCamera() != nullptr)
    {
        float border = 1;
        ui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, border);
        ui::PushStyleColor(ImGuiCol_Border, ui::GetColorU32(ImGuiCol_Border, 255.f));                                   // Make a border opaque, otherwise it is barely visible.
        ui::SetCursorScreenPos(rect.Max - ToImGui(cameraPreviewSize) - ImVec2{10, 10});

        ui::Image(cameraPreviewTexture_.Get(), ToImGui(cameraPreviewSize));
        ui::RenderFrameBorder(ui::GetItemRectMin() - ImVec2{border, border}, ui::GetItemRectMax() + ImVec2{border, border});

        ui::PopStyleColor();
        ui::PopStyleVar();
    }

    // Prevent dragging window when scene view is clicked.
    if (ui::IsWindowHovered())
        windowFlags_ |= ImGuiWindowFlags_NoMove;
    else
        windowFlags_ &= ~ImGuiWindowFlags_NoMove;

    if (!gizmo_.IsActive() && (isClickedLeft_ || isClickedRight_) && !isViewportActive_)
    {
        // Handle object selection.
        ImGuiIO& io = ui::GetIO();
        Ray cameraRay = GetCamera()->GetScreenRay(
            (io.MousePos.x - viewportRect.Min.x) / viewportRect.GetWidth(),
            (io.MousePos.y - viewportRect.Min.y) / viewportRect.GetHeight());
        // Pick only geometry objects, not eg. zones or lights, only get the first (closest) hit
        ea::vector<RayQueryResult> results;

        RayOctreeQuery query(results, cameraRay, RAY_TRIANGLE, M_INFINITY, DRAWABLE_GEOMETRY);
        GetScene()->GetComponent<Octree>()->RaycastSingle(query);

        if (!results.size())
        {
            // When object geometry was not hit by a ray - query for object bounding box.
            RayOctreeQuery query2(results, cameraRay, RAY_OBB, M_INFINITY, DRAWABLE_GEOMETRY);
            GetScene()->GetComponent<Octree>()->RaycastSingle(query2);
        }

        if (results.size() && results[0].drawable_->GetNode() != nullptr)
        {
            StringHash componentType;
            WeakPtr<Node> clickNode(results[0].drawable_->GetNode());

            if (clickNode->HasTag("DebugIcon"))
                componentType = clickNode->GetVar("ComponentType").GetStringHash();

            while (!clickNode.Expired() && clickNode->HasTag("__EDITOR_OBJECT__"))
                clickNode = clickNode->GetParent();

            if (isClickedLeft_ || (isClickedRight_ && ImLengthSqr(ui::GetMouseDragDelta(MOUSEB_RIGHT)) == 0.0f))
            {
                if (!ui::IsKeyDown(KEY_CTRL))
                    ClearSelection();

                if (clickNode == GetScene())
                {
                    if (componentType != StringHash::ZERO)
                    {
                        Component* component = clickNode->GetComponent(componentType);
                        if (!IsSelected(component))
                        {
                            ClearSelection();
                            ToggleSelection(component);
                        }
                    }
                }
                else if (!IsSelected(clickNode))
                    ToggleSelection(clickNode);

                if (isClickedRight_)
                    ui::OpenPopupEx(ui::GetID("Node context menu"));
            }
        }
        else if (isClickedLeft_)
        {
            ClearSelection();
            OnNodeSelectionChanged();
        }
    }

    RenderNodeContextMenu();

    if (debugHudVisible_)
    {
        if (auto* hud = GetSubsystem<DebugHud>())
        {
            ImFont* monospaceFont = GetSubsystem<Editor>()->GetMonoSpaceFont();
            ui::PushFont(monospaceFont);
            ImGuiWindow* window = ui::GetCurrentWindow();
            // FIXME: Without this FPS has extra space below.
            ui::GetCurrentWindow()->DC.CurrLineSize.y = monospaceFont->FontSize;
            ui::SetCursorScreenPos(rect.Min + ImVec2{10, 10 + ui::GetIO().Fonts->Fonts[0]->FontSize});
            hud->RenderUI(DEBUGHUD_SHOW_ALL);
            ui::PopFont();
        }
    }

    ui::EndChild(); // Scene view

    BaseClassName::RenderWindowContent();

    return open;
}

bool SceneTab::LoadResource(const ea::string& resourcePath)
{
    UndoTrackGuard noTrack(undo_, false);

    if (resourcePath == GetResourceName())
        // Already loaded.
        return true;

    if (!BaseClassName::LoadResource(resourcePath))
        return false;

    auto cache = GetSubsystem<ResourceCache>();
    auto manager = GetSubsystem<SceneManager>();
    Scene* scene = manager->GetOrCreateScene(GetFileName(resourcePath));

    undo_->Connect(scene, this);

    manager->SetActiveScene(scene);

    bool loaded = false;
    if (resourcePath.ends_with(".xml", false))
    {
        auto* file = cache->GetResource<XMLFile>(resourcePath);
        loaded = file && scene->LoadXML(file->GetRoot());
        if (file)
            scene->SetFileName(file->GetNativeFileName());
    }
    else if (resourcePath.ends_with(".json", false))
    {
        auto* file = cache->GetResource<JSONFile>(resourcePath);
        loaded = file && scene->LoadJSON(file->GetRoot());
        if (file)
            scene->SetFileName(file->GetNativeFileName());
    }
    else
    {
        URHO3D_LOGERRORF("Unknown scene file format %s", GetExtension(resourcePath).c_str());
        manager->UnloadScene(scene);
        cache->ReleaseResource(XMLFile::GetTypeStatic(), resourcePath, true);
        return false;
    }

    if (!loaded)
    {
        URHO3D_LOGERRORF("Loading scene %s failed", GetFileName(resourcePath).c_str());
        manager->UnloadScene(scene);
        cache->ReleaseResource(XMLFile::GetTypeStatic(), resourcePath, true);
        return false;
    }

    manager->UnloadAllButActiveScene();
    scene->SetUpdateEnabled(false);    // Scene is updated manually.
    scene->GetOrCreateComponent<Octree>();
    scene->GetOrCreateComponent<EditorSceneSettings>(LOCAL);

    GetSubsystem<Editor>()->UpdateWindowTitle(resourcePath);

    return true;
}

bool SceneTab::SaveResource()
{
    if (GetScene() == nullptr)
        return false;

    if (!BaseClassName::SaveResource())
        return false;

    context_->GetSubsystem<ResourceCache>()->ReleaseResource(XMLFile::GetTypeStatic(), resourceName_, true);

    auto fullPath = context_->GetSubsystem<ResourceCache>()->GetResourceFileName(resourceName_);
    if (fullPath.empty())
        return false;

    File file(context_, fullPath, FILE_WRITE);
    bool result = false;

    float elapsed = GetScene()->GetElapsedTime();
    GetScene()->SetElapsedTime(0);
    GetScene()->SetUpdateEnabled(true);
    if (fullPath.ends_with(".xml", false))
        result = GetScene()->SaveXML(file);
    else if (fullPath.ends_with(".json", false))
        result = GetScene()->SaveJSON(file);
    GetScene()->SetUpdateEnabled(false);
    GetScene()->SetElapsedTime(elapsed);

    if (result)
        SendEvent(E_EDITORRESOURCESAVED);
    else
        URHO3D_LOGERRORF("Saving scene to %s failed.", resourceName_.c_str());

    return result;
}

void SceneTab::RenderToolbarButtons()
{
    ui::SetCursorPos({4, 4});

    if (ui::EditorToolbarButton(ICON_FA_SAVE, "Save"))
        SaveResource();

    ui::SameLine(0, 3.f);

    ui::BeginButtonGroup();
    if (ui::EditorToolbarButton(ICON_FA_ARROWS_ALT "###Translate", "Translate", gizmo_.GetOperation() == GIZMOOP_TRANSLATE))
        gizmo_.SetOperation(GIZMOOP_TRANSLATE);
    if (ui::EditorToolbarButton(ICON_FA_SYNC "###Rotate", "Rotate", gizmo_.GetOperation() == GIZMOOP_ROTATE))
        gizmo_.SetOperation(GIZMOOP_ROTATE);
    if (ui::EditorToolbarButton(ICON_FA_EXPAND_ARROWS_ALT "###Scale", "Scale", gizmo_.GetOperation() == GIZMOOP_SCALE))
        gizmo_.SetOperation(GIZMOOP_SCALE);
    ui::EndButtonGroup();

    ui::SameLine(0, 3.f);

    ui::BeginButtonGroup();
    if (ui::EditorToolbarButton(ICON_FA_ARROWS_ALT "###World", "World", gizmo_.GetTransformSpace() == TS_WORLD))
        gizmo_.SetTransformSpace(TS_WORLD);
    if (ui::EditorToolbarButton(ICON_FA_EXPAND_ARROWS_ALT "###Local", "Local", gizmo_.GetTransformSpace() == TS_LOCAL))
        gizmo_.SetTransformSpace(TS_LOCAL);
    ui::EndButtonGroup();

    ui::SameLine(0, 3.f);

    if (auto* settings = GetScene()->GetComponent<EditorSceneSettings>())
    {
        ui::BeginButtonGroup();
        if (ui::EditorToolbarButton("3D", "3D mode in editor viewport.", !settings->GetCamera2D()))
            settings->SetCamera2D(false);
        if (ui::EditorToolbarButton("2D", "2D mode in editor viewport.", settings->GetCamera2D()))
            settings->SetCamera2D(true);
        ui::EndButtonGroup();
    }
    ui::SameLine(0, 3.f);

    if (auto* hud = GetSubsystem<DebugHud>())
    {
        if (ui::EditorToolbarButton(ICON_FA_BUG, "Display debug hud.", debugHudVisible_))
            debugHudVisible_ ^= true;
    }

    ui::SameLine(0, 3.f);

    SendEvent(E_EDITORTOOLBARBUTTONS);

    ui::SameLine(0, 3.f);
}

bool SceneTab::IsSelected(Node* node) const
{
    if (node == nullptr)
        return false;

    return selectedNodes_.contains(WeakPtr(node));
}

bool SceneTab::IsSelected(Component* component) const
{
    if (component == nullptr)
        return false;

    return selectedComponents_.contains(WeakPtr(component));
}

void SceneTab::OnNodeSelectionChanged()
{
    using namespace EditorSelectionChanged;
    SendEvent(E_EDITORSELECTIONCHANGED, P_TAB, this);

    auto* inspector = GetSubsystem<InspectorTab>();
    auto* editor = GetSubsystem<Editor>();
    int inspectedNodes = 0;
    int inspectedComponents = 0;
    Node* lastInspectedNode = nullptr;

    inspector->Clear();

    // Inspect all selected nodes.
    for (Node* node : selectedNodes_)
    {
        if (node)
        {
            inspector->Inspect(node, GetScene());
            inspectedNodes++;
            lastInspectedNode = node;
        }
    }
    // And all selected components.
    for (Component* component : selectedComponents_)
    {
        if (component)
        {
            inspector->Inspect(component, GetScene());
            inspectedComponents++;
        }
    }
    // However if we selected a single node and no components - inspect all components of that node for convenience.
    if (inspectedNodes == 1 && inspectedComponents == 0)
    {
        for (Component* component : lastInspectedNode->GetComponents())
            inspector->Inspect(component, GetScene());
    }

    editor->GetTab<HierarchyTab>()->SetProvider(this);
}

void SceneTab::RenderHierarchy()
{
    if (performRangeSelectionFrame_ != ui::GetFrameCount())
        selectionRect_ = {ui::GetMousePos(), ui::GetMousePos()};

    if (auto* scene = GetScene())
    {
        ui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 10);
        RenderNodeTree(scene);
        ui::PopStyleVar();
    }

    // Perform range selection on next frame. Only then we can have a rectangle covering selection ends
    // which we need to do range selection.
    if (ui::IsWindowHovered())
    {
        if (ui::IsMouseClicked(MOUSEB_LEFT) && ui::IsKeyDown(SCANCODE_SHIFT) && !selectedNodes_.empty())
            performRangeSelectionFrame_ = ui::GetFrameCount() + 1;
    }
}

void SceneTab::RenderNodeTree(Node* node)
{
    if (node == nullptr)
        return;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
        | ImGuiTreeNodeFlags_OpenOnDoubleClick
        | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (node->GetParent() == nullptr)
        flags |= ImGuiTreeNodeFlags_DefaultOpen;

    if (node->IsTemporary() || node->HasTag("__EDITOR_OBJECT__"))
        return;

    if (node == scrollTo_.Get())
        ui::SetScrollHereY();

    ea::string name = node->GetName().empty() ? ToString("%s %d", node->GetTypeName().c_str(), node->GetID()) : node->GetName();
    if (IsSelected(node))
        flags |= ImGuiTreeNodeFlags_Selected;

    ui::Image("Node");
    ui::SameLine();
    ui::PushID((void*)node);
    auto opened = ui::TreeNodeEx(name.c_str(), flags);
    ImRect treeNodeRect = {ui::GetItemRectMin(), ui::GetItemRectMax()};
    auto it = openHierarchyNodes_.find(node);
    if (it != openHierarchyNodes_.end())
    {
        if (!opened)
        {
            ui::OpenTreeNode(ui::GetCurrentWindow()->GetID(name.c_str()));
            opened = true;
        }
        openHierarchyNodes_.erase(it);
    }

    if (ui::BeginDragDropSource())
    {
        ui::SetDragDropVariant(node->GetType().ToString(), node);
        ui::TextUnformatted(name.c_str());
        ui::EndDragDropSource();
    }

    if (ui::BeginDragDropTarget())
    {
        const Variant& payload = ui::AcceptDragDropVariant(Node::GetTypeStatic().ToString());
        if (!payload.IsEmpty())
        {
            SharedPtr<Node> child(dynamic_cast<Node*>(payload.GetPtr()));
            if (child && child != node)
            {
                node->AddChild(child);
                if (!opened)
                    openHierarchyNodes_.push_back(node);
            }
        }
        ui::EndDragDropTarget();
    }

    if (!opened)
    {
        // If TreeNode above is opened, it pushes it's label as an ID to the stack. However if it is not open then no
        // ID is pushed. This creates a situation where context menu is not properly attached to said tree node due to
        // missing ID on the stack. To correct this we ensure that ID is always pushed. This allows us to show context
        // menus even for closed tree nodes.
        ui::PushID(name.c_str());
    }

    // Popup may delete node. Weak reference will convey that information.
    WeakPtr<Node> nodeRef(node);

    if (ui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
    {
        if (ui::IsMouseClicked(MOUSEB_LEFT))
        {
            // Select single node.
            if (!ui::IsKeyDown(SCANCODE_SHIFT) && !ui::IsKeyDown(SCANCODE_CTRL))
                ClearSelection();

            if (ui::IsKeyDown(SCANCODE_SHIFT))
                Select(node);
            else
                ToggleSelection(node);
        }
        else if (ui::IsMouseReleased(MOUSEB_RIGHT) && ImLengthSqr(ui::GetMouseDragDelta(MOUSEB_RIGHT)) == 0.0f)
        {
            if (!IsSelected(node))
            {
                ClearSelection();
                ToggleSelection(node);
            }
            ui::OpenPopupEx(ui::GetID("Node context menu"));
        }
    }

    if (IsSelected(node))
        selectionRect_.Add(treeNodeRect);

    // Range-select by shift-clicking an item.
    if (performRangeSelectionFrame_ == ui::GetFrameCount())
    {
        if (selectionRect_.Contains(treeNodeRect.Min))
        {
            Select(node);
            selectionRect_.Add(treeNodeRect.Min);
        }
    }

    RenderNodeContextMenu();

    if (opened)
    {
        if (!nodeRef.Expired())
        {
            ea::vector<SharedPtr<Component>> components = node->GetComponents();
            for (const auto& component: components)
            {
                if (component->IsTemporary())
                    continue;

                ui::PushID(component);

                ui::Image(component->GetTypeName());
                ui::SameLine();

                bool selected = selectedComponents_.contains(component);
                ui::Selectable(component->GetTypeName().c_str(), selected);

                if (ui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
                {
                    if (ui::IsMouseClicked(MOUSEB_LEFT))
                    {
                        if (!ui::IsKeyDown(SCANCODE_CTRL))
                            ClearSelection();
                        Select(component);
                    }
                    else if (ui::IsMouseReleased(MOUSEB_RIGHT) && ImLengthSqr(ui::GetMouseDragDelta(MOUSEB_RIGHT)) == 0.0f)
                    {
                        if (!IsSelected(component))
                        {
                            ClearSelection();
                            Select(component);
                        }
                        ui::OpenPopupEx(ui::GetID("Node context menu"));
                    }
                }


                RenderNodeContextMenu();

                ui::PopID();
            }

            // Do not use element->GetChildren() because child may be deleted during this loop.
            ea::vector<Node*> children;
            node->GetChildren(children);
            for (Node* child : children)
            {
                // ensure the tree is expanded to the currently selected node if there is one node selected.
                if (selectedNodes_.size() == 1)
                {
                    for (auto& selectedNode : selectedNodes_)
                    {
                        if (selectedNode.Expired())
                            continue;

                        if (selectedNode->IsChildOf(child))
                            ui::SetNextItemOpen(true);
                    }
                }

                //recursive call.
                RenderNodeTree(child);
            }
        }
        ui::TreePop();
    }
    else
        ui::PopID();
    ui::PopID();
}

void SceneTab::RemoveSelection()
{
    for (auto& component : selectedComponents_)
    {
        if (!component.Expired())
            component->Remove();
    }

    for (auto& selected : selectedNodes_)
    {
        if (!selected.Expired())
            selected->Remove();
    }

    ClearSelection();
}

Scene* SceneTab::GetScene()
{
    return GetSubsystem<SceneManager>()->GetActiveScene();
}

void SceneTab::OnUpdate(VariantMap& args)
{
    Scene* scene = GetScene();
    if (scene == nullptr)
        return;

    float timeStep = args[Update::P_TIMESTEP].GetFloat();
    bool isMode3D_ = !scene->GetComponent<EditorSceneSettings>()->GetCamera2D();
    StringHash cameraControllerType = isMode3D_ ? DebugCameraController3D::GetTypeStatic() : DebugCameraController2D::GetTypeStatic();
    if (Camera* camera = GetCamera())
    {
        auto* component = static_cast<DebugCameraController*>(camera->GetComponent(cameraControllerType));
        if (component != nullptr)
        {
            if (isViewportActive_)
            {
                ui::SetMouseCursor(ImGuiMouseCursor_None);
                if (isMode3D_)
                {
                    auto* controller3D = static_cast<DebugCameraController3D*>(component);
                    Vector3 center;
                    int selectionCount = gizmo_.GetSelectionCenter(center, selectedNodes_);
                    if (selectionCount > 0)
                    {
                        controller3D->SetRotationCenter(center);
                    }
                    else
                    {
                        controller3D->ClearRotationCenter();
                    }
                }
                component->RunFrame(timeStep);
            }
        }
    }

    if (Tab* tab = GetSubsystem<Editor>()->GetActiveTab())
    {
        StringHash activeTabType = tab->GetType();
        if (activeTabType == GetType() || activeTabType == HierarchyTab::GetTypeStatic())
        {
            if (!ui::IsAnyItemActive())
            {
                // Global view hotkeys
                if (ui::IsKeyPressed(KEY_DELETE))
                    RemoveSelection();
                else if (ui::IsKeyPressed(KEY_CTRL))
                {
                    if (ui::IsKeyPressed(KEY_C))
                        CopySelection();
                    else if (ui::IsKeyPressed(KEY_V))
                    {
                        if (ui::IsKeyDown(KEY_SHIFT))
                            PasteIntoSelection();
                        else
                            PasteIntuitive();
                    }
                }
                else if (ui::IsKeyPressed(KEY_ESCAPE))
                    ClearSelection();
            }
        }

        if (tab == this)
        {
            if (!isViewportActive_)
            {
                if (ui::IsKeyPressed(KEY_W))
                {
                    gizmo_.SetOperation(GIZMOOP_TRANSLATE);
                }
                else if (ui::IsKeyPressed(KEY_E))
                {
                    gizmo_.SetOperation(GIZMOOP_ROTATE);
                }
                else if (ui::IsKeyPressed(KEY_R))
                {
                    gizmo_.SetOperation(GIZMOOP_SCALE);
                }
            }
        }
    }
}

void SceneTab::SaveState(SceneState& destination)
{
    UndoTrackGuard tracking(undo_, false);

    // Preserve current selection
    savedNodeSelection_.clear();
    savedComponentSelection_.clear();
    for (auto& node : selectedNodes_)
    {
        if (node)
            savedNodeSelection_.push_back(node->GetID());
    }
    for (auto& component : selectedComponents_)
    {
        if (component)
            savedComponentSelection_.push_back(component->GetID());
    }

    destination.Save(GetScene());
}

void SceneTab::RestoreState(SceneState& source)
{
    UndoTrackGuard tracking(undo_, false);

    VectorBuffer editorObjectsState;
    if (Node* editorObjects = GetScene()->GetChild("EditorObjects"))
    {
        // Preserve state of editor camera.
        editorObjects->SetTemporary(false);
        editorObjects->Save(editorObjectsState);
    }

    source.Load(GetScene());
    GetSubsystem<SceneManager>()->UnloadAllButActiveScene();

    if (editorObjectsState.GetSize() > 0)
    {
        if (Node* objects = GetScene()->GetChild("EditorObjects"))
            objects->Remove();

        // Make sure camera is not reset upon stopping scene simulation.
        editorObjectsState.Seek(0);
        auto nodeID = editorObjectsState.ReadUInt();
        Node* editorObjects = GetScene()->CreateChild("EditorObjects", LOCAL, nodeID, true);
        editorObjectsState.Seek(0);
        editorObjects->Load(editorObjectsState);
    }

    // Restore previous selection
    ClearSelection();
    for (unsigned id : savedNodeSelection_)
        Select(GetScene()->GetNode(id));

    for (unsigned id : savedComponentSelection_)
        Select(GetScene()->GetComponent(id));
    savedNodeSelection_.clear();
    savedComponentSelection_.clear();
}

void SceneTab::RenderNodeContextMenu()
{
    if ((!selectedNodes_.empty() || !selectedComponents_.empty()) && ui::BeginPopup("Node context menu"))
    {
        if (ui::IsKeyPressed(KEY_ESCAPE) || isViewportActive_)
        {
            // Close when interacting with scene camera.
            ui::CloseCurrentPopup();
            ui::EndPopup();
            return;
        }

        if (!selectedNodes_.empty())
        {
            bool alternative = ui::IsKeyDown(KEY_SHIFT);

            if (ui::MenuItem(alternative ? "Create Child (Local)" : "Create Child"))
            {
                ea::vector<Node*> newNodes;
                for (auto& selectedNode : selectedNodes_)
                {
                    if (!selectedNode.Expired())
                    {
                        newNodes.push_back(selectedNode->CreateChild(EMPTY_STRING, alternative ? LOCAL : REPLICATED));
                        openHierarchyNodes_.push_back(selectedNode);
                        openHierarchyNodes_.push_back(newNodes.back());
                        scrollTo_ = newNodes.back();
                    }
                }

                ClearSelection();
                Select(newNodes);
            }

            if (ui::BeginMenu(alternative ? "Create Component (Local)" : "Create Component"))
            {
                auto* editor = GetSubsystem<Editor>();
                auto categories = context_->GetObjectCategories().keys();
#if URHO3D_RMLUI
                categories.erase_first("UI");
#endif
                ea::quick_sort(categories.begin(), categories.end());

                for (const ea::string& category : categories)
                {
                    auto components = editor->GetObjectsByCategory(category);
                    if (components.empty())
                        continue;

                    if (ui::BeginMenu(category.c_str()))
                    {
                        ea::quick_sort(components.begin(), components.end());

                        for (const ea::string& component : components)
                        {
                            ui::Image(component);
                            ui::SameLine();
                            if (ui::MenuItem(component.c_str()))
                            {
                                for (auto& selectedNode : selectedNodes_)
                                {
                                    if (!selectedNode.Expired())
                                    {
                                        if (selectedNode->CreateComponent(StringHash(component), alternative ? LOCAL : REPLICATED))
                                        {
                                            openHierarchyNodes_.push_back(selectedNode);
                                            OnNodeSelectionChanged();
                                        }
                                    }
                                }
                            }
                        }
                        ui::EndMenu();
                    }
                }
                ui::EndMenu();
            }
            ui::Separator();
        }

        if (ui::MenuItem(ICON_FA_COPY " Copy", "Ctrl+C"))
            CopySelection();

        if (ui::MenuItem(ICON_FA_PASTE " Paste", "Ctrl+V"))
            PasteIntuitive();

        if (ui::MenuItem(ICON_FA_PASTE " Paste Into", "Ctrl+Shift+V"))
            PasteIntoSelection();

        if (ui::MenuItem(ICON_FA_TRASH " Delete", "Del"))
            RemoveSelection();

        ui::Separator();

        if (ui::BeginMenu("Debug Info"))
        {
            enum class DebugInfoMode
            {
                AUTOMATIC,
                ALWAYS,
                NEVER,
                MISMATCH,
                NONE,
            } debugMode = DebugInfoMode::NONE;

            for (auto& node : selectedNodes_)
            {
                if (!node)
                    continue;

                DebugInfoMode nodeMode;
                if (node->HasTag("DebugInfoNever"))
                    nodeMode = DebugInfoMode::NEVER;
                else if (node->HasTag("DebugInfoAlways"))
                    nodeMode = DebugInfoMode::ALWAYS;
                else
                    nodeMode = DebugInfoMode::AUTOMATIC;

                if (debugMode == DebugInfoMode::NONE)
                    debugMode = nodeMode;
                else if (debugMode != nodeMode)
                {
                    debugMode = DebugInfoMode::MISMATCH;
                    break;
                }
            }

            bool setDebugMode = false;
            if (ui::MenuItem("Automatic", nullptr, debugMode == DebugInfoMode::AUTOMATIC))
            {
                debugMode = DebugInfoMode::AUTOMATIC;
                setDebugMode = true;
            }
            if (ui::MenuItem("Always", nullptr, debugMode == DebugInfoMode::ALWAYS))
            {
                debugMode = DebugInfoMode::ALWAYS;
                setDebugMode = true;
            }
            if (ui::MenuItem("Never", nullptr, debugMode == DebugInfoMode::NEVER))
            {
                debugMode = DebugInfoMode::NEVER;
                setDebugMode = true;
            }

            if (setDebugMode)
            {
                for (auto& node : selectedNodes_)
                {
                    if (!node)
                        continue;

                    if (debugMode == DebugInfoMode::AUTOMATIC)
                    {
                        node->RemoveTag("DebugInfoAlways");
                        node->RemoveTag("DebugInfoNever");
                    }
                    if (debugMode == DebugInfoMode::ALWAYS)
                    {
                        node->AddTag("DebugInfoAlways");
                        node->RemoveTag("DebugInfoNever");
                    }
                    if (debugMode == DebugInfoMode::NEVER)
                    {
                        node->RemoveTag("DebugInfoAlways");
                        node->AddTag("DebugInfoNever");
                    }
                }
            }
            ui::EndMenu();
        }

        ui::EndPopup();
    }
}

void SceneTab::OnComponentAdded(VariantMap& args)
{
    using namespace ComponentAdded;
    auto* component = static_cast<Component*>(args[P_COMPONENT].GetPtr());
    AddComponentIcon(component);
}

void SceneTab::OnComponentRemoved(VariantMap& args)
{
    using namespace ComponentRemoved;
    auto* component = static_cast<Component*>(args[P_COMPONENT].GetPtr());
    RemoveComponentIcon(component);
}

void SceneTab::OnTemporaryChanged(VariantMap& args)
{
    using namespace TemporaryChanged;
    if (Component* component = dynamic_cast<Component*>(args[P_SERIALIZABLE].GetPtr()))
    {
        if (component->IsTemporary())
            RemoveComponentIcon(component);
        else
            AddComponentIcon(component);
    }
}

void SceneTab::OnSceneActivated(VariantMap& args)
{
    using namespace SceneActivated;
    if (auto* scene = static_cast<Scene*>(args[P_NEWSCENE].GetPtr()))
    {
        SubscribeToEvent(scene, E_COMPONENTADDED, [this](StringHash, VariantMap& args) { OnComponentAdded(args); });
        SubscribeToEvent(scene, E_COMPONENTREMOVED, [this](StringHash, VariantMap& args) { OnComponentRemoved(args); });
        SubscribeToEvent(scene, E_TEMPORARYCHANGED, [this](StringHash, VariantMap& args) { OnTemporaryChanged(args); });

        undo_->Connect(scene, this);

        // If application logic switches to another scene it will have updates enabled. Ensure they are disabled so we do not get double
        // updates per frame.
        scene->SetUpdateEnabled(false);
        cameraPreviewViewport_->SetScene(scene);
        viewport_->SetScene(scene);
        ClearSelection();
    }
}

void SceneTab::OnEditorProjectClosing()
{
    ea::string snapshotPath = GetSubsystem<Project>()->GetProjectPath() + ".snapshot.png";

    if (texture_.Null() || GetScene() == nullptr)
    {
        GetSubsystem<FileSystem>()->Delete(snapshotPath);
        return;
    }

    // Also crop image to a square.
    SharedPtr<Image> snapshot = texture_->GetImage();
    if (!snapshot)
    {
        return;
    }

    int w = snapshot->GetWidth();
    int h = snapshot->GetHeight();
    int side = Min(w, h);
    IntRect rect{0, 0, w, h};
    if (w > h)
    {
        rect.left_ = ((w - side) / 2);
        rect.right_ = rect.left_ + side;
    }
    else if (h > w)
    {
        rect.top_ = ((w - side) / 2);
        rect.bottom_ = rect.top_ + side;
    }
    SharedPtr<Image> cropped = snapshot->GetSubimage(rect);
    cropped->SavePNG(snapshotPath);
}

void SceneTab::AddComponentIcon(Component* component)
{
    if (component == nullptr || component->IsTemporary())
        return;

    Node* node = component->GetNode();

    if (node == nullptr || node->IsTemporary() || node->HasTag("__EDITOR_OBJECT__") ||
        (node->GetName().starts_with("__") && node->GetName().ends_with("__")))
        return;

    auto* material = context_->GetSubsystem<ResourceCache>()->GetResource<Material>(Format("Materials/Editor/DebugIcon{}.xml", component->GetTypeName()), false);
    if (material != nullptr)
    {
        auto iconTag = "DebugIcon" + component->GetTypeName();
        if (!node->GetChildrenWithTag(iconTag).empty())
            return;                                                 // Icon for component of this type is already added

        UndoTrackGuard tracking(undo_, false);
        int count = node->GetChildrenWithTag("DebugIcon").size();
        node = node->CreateChild();                                 // !! from now on `node` is a container for debug icon
        node->AddTag("DebugIcon");
        node->AddTag(iconTag);
        node->AddTag("__EDITOR_OBJECT__");
        node->SetVar("ComponentType", component->GetType());
        node->SetTemporary(true);

        auto* billboard = node->CreateComponent<BillboardSet>();
        billboard->SetFaceCameraMode(FaceCameraMode::FC_LOOKAT_XYZ);
        if (auto* settings = GetScene()->GetComponent<EditorSceneSettings>())
        {
            bool is2D = settings->GetCamera2D();
            if (is2D)
            {
                node->LookAt(node->GetWorldPosition() + Vector3::FORWARD);
                billboard->SetFaceCameraMode(FaceCameraMode::FC_NONE);
            }
        }

        billboard->SetNumBillboards(1);
        billboard->SetMaterial(material);
        billboard->SetViewMask(EDITOR_VIEW_LAYER);

        if (auto* bb = billboard->GetBillboard(0))
        {
            auto* graphics = context_->GetSubsystem<Graphics>();
            float scale = graphics->GetDisplayDPI(graphics->GetCurrentMonitor()).z_ / 96.f;
            bb->size_ = Vector2::ONE * 0.2f * scale;
            bb->enabled_ = true;
            bb->position_ = {0, (float)count * 0.4f * scale, 0};
        }
    }
}

void SceneTab::RemoveComponentIcon(Component* component)
{
    if (component == nullptr || component->IsTemporary())
        return;

    Node* node = component->GetNode();
    if (node->IsTemporary())
        return;

    UndoTrackGuard tracking(undo_, false);

    for (auto* icon : node->GetChildrenWithTag("DebugIcon" + component->GetTypeName()))
        icon->Remove();

    int index = 0;
    for (auto* icon : node->GetChildrenWithTag("DebugIcon"))
    {
        if (auto* billboard = icon->GetComponent<BillboardSet>())
        {
            billboard->GetBillboard(0)->position_ = {0, index * 0.4f, 0};
            billboard->Commit();
            index++;
        }
    }
}

void SceneTab::OnFocused()
{
    auto* editor = GetSubsystem<Editor>();
    editor->GetTab<HierarchyTab>()->SetProvider(this);
}

void SceneTab::ModifySelection(const ea::vector<Node*>& nodes, const ea::vector<Component*>& components, SceneTab::SelectionMode mode)
{
    int numSelectedNodes = selectedNodes_.size();
    int numSelectedComponents = selectedComponents_.size();

    switch (mode)
    {
    case SelectionMode::Add:
    {
        for (Node* node : nodes)
            selectedNodes_.insert(WeakPtr(node));
        for (Component* component : components)
            selectedComponents_.insert(WeakPtr(component));

        if (selectedNodes_.size() != numSelectedNodes || selectedComponents_.size() != numSelectedComponents)
            OnNodeSelectionChanged();
        break;
    }
    case SelectionMode::Subtract:
    {
        for (Node* node : nodes)
            selectedNodes_.erase(WeakPtr(node));
        for (Component* component : components)
            selectedComponents_.erase(WeakPtr(component));

        if (selectedNodes_.size() != numSelectedNodes || selectedComponents_.size() != numSelectedComponents)
            OnNodeSelectionChanged();
        break;
    }
    case SelectionMode::Toggle:
    {
        for (Node* node : nodes)
        {
            WeakPtr nodePtr(node);
            if (selectedNodes_.contains(nodePtr))
                selectedNodes_.erase(nodePtr);
            else
                selectedNodes_.insert(nodePtr);
        }
        for (Component* component : components)
        {
            WeakPtr componentPtr(component);
            if (selectedComponents_.contains(componentPtr))
                selectedComponents_.erase(componentPtr);
            else
                selectedComponents_.insert(componentPtr);
        }
        if (!nodes.empty() || components.empty())
            OnNodeSelectionChanged();
        break;
    }
    }
}

void SceneTab::ClearSelection()
{
    if (!selectedNodes_.empty() || !selectedComponents_.empty())
    {
        selectedNodes_.clear();
        selectedComponents_.clear();
    }
}

bool SceneTab::SerializeSelection(Archive& archive)
{
    if (auto block = archive.OpenSequentialBlock("items"))
    {
        if (auto block = archive.OpenArrayBlock("nodes", selectedNodes_.size()))
        {
            unsigned id;
            if (archive.IsInput())
            {
                selectedNodes_.clear();
                Scene* scene = GetScene();
                for (unsigned i = 0; i < block.GetSizeHint(); ++i)
                {
                    if (!SerializeValue(archive, "id", id))
                        return false;
                    if (Node* node = scene->GetNode(id))
                        selectedNodes_.insert(WeakPtr(node));
                    else
                        return false;
                }
            }
            else
            {
                for (Node* node : selectedNodes_)
                {
                    if (node == nullptr)
                        continue;
                    id = node->GetID();
                    if (!SerializeValue(archive, "id", id))
                        return false;
                }
            }
        }

        if (auto block = archive.OpenArrayBlock("components", selectedComponents_.size()))
        {
            unsigned id;
            if (archive.IsInput())
            {
                selectedComponents_.clear();
                Scene* scene = GetScene();
                for (unsigned i = 0; i < block.GetSizeHint(); ++i)
                {
                    if (!SerializeValue(archive, "id", id))
                        return false;
                    if (Component* component = scene->GetComponent(id))
                        selectedComponents_.insert(WeakPtr(component));
                    else
                        return false;
                }
            }
            else
            {
                for (Component* component : selectedComponents_)
                {
                    if (component == nullptr)
                        continue;
                    id = component->GetID();
                    if (!SerializeValue(archive, "id", id))
                        return false;
                }
            }
        }

        if (archive.IsInput())
            OnNodeSelectionChanged();

        return true;
    }
    return false;
}

void SceneTab::UpdateCameras()
{
    cameraPreviewViewport_->SetCamera(nullptr);

    if (selectedNodes_.size() == 1)
    {
        if (Node* node = *selectedNodes_.begin())
        {
            if (Camera* camera = node->GetComponent<Camera>())
            {
                camera->SetViewMask(camera->GetViewMask() & ~EDITOR_VIEW_LAYER);
                cameraPreviewViewport_->SetCamera(camera);
                cameraPreviewViewport_->SetRenderPath(GetViewport()->GetRenderPath());
            }
        }
    }

    if (Camera* camera = GetCamera())
    {
        viewport_->SetCamera(camera);
        if (auto* debug = GetScene()->GetComponent<DebugRenderer>())
            debug->SetView(camera);
    }
}

void SceneTab::CopySelection()
{
    const auto& selection = selectedNodes_;
    if (!selection.empty())
    {
        clipboard_.Clear();
        clipboard_.Copy(selectedNodes_);
    }
    else if (!selectedComponents_.empty())
    {
        clipboard_.Clear();
        clipboard_.Copy(selectedComponents_);
    }
}

void SceneTab::PasteNextToSelection()
{
    const auto& selection = selectedNodes_;
    PasteResult result;
    Node* target = nullptr;
    if (!selection.empty())
    {
        target = nullptr;
        for (auto it = selection.begin(); it != selection.end() && target == nullptr; it++)   // Selected node may be null
            target = *it;
        if (target != nullptr)
            target = target->GetParent();
    }

    if (target == nullptr)
        target = GetScene();

    result = clipboard_.Paste(target);

    ClearSelection();
    OnNodeSelectionChanged();

    for (Node* node : result.nodes_)
        Select(node);

    for (Component* component : result.components_)
        Select(component);
}

void SceneTab::PasteIntoSelection()
{
    const auto& selection = selectedNodes_;
    PasteResult result;
    if (selection.empty())
        result = clipboard_.Paste(GetScene());
    else
        result = clipboard_.Paste(selection);

    ClearSelection();
    OnNodeSelectionChanged();

    for (Node* node : result.nodes_)
        Select(node);

    for (Component* component : result.components_)
        Select(WeakPtr<Component>(component));
}

void SceneTab::PasteIntuitive()
{
    if (clipboard_.HasNodes())
        PasteNextToSelection();
    else if (clipboard_.HasComponents())
        PasteIntoSelection();
}

Camera* SceneTab::GetCamera()
{
    if (Scene* scene = GetScene())
    {
        if (Node* node = scene->GetChild("__EditorCamera__", true))
            return node->GetComponent<Camera>();
    }
    return nullptr;
}

void SceneTab::RenderDebugInfo()
{
    Scene* scene = GetScene();
    if (scene == nullptr)
        return;

    DebugRenderer* debug = scene->GetComponent<DebugRenderer>();
    if (debug == nullptr)
        return;

    auto renderDebugInfo = [debug](Component* component) {
        if (auto* light = component->Cast<Light>())
            light->DrawDebugGeometry(debug, true);
        else if (auto* drawable = component->Cast<Drawable>())
            debug->AddBoundingBox(drawable->GetWorldBoundingBox(), Color::WHITE);
        else if (!component->IsInstanceOf<Terrain>())
            component->DrawDebugGeometry(debug, true);
    };

    const auto& selection = selectedNodes_;
    for (auto& node : selection)
    {
        if (node && !node->HasTag("DebugInfoNever"))
        {
            for (auto& component: node->GetComponents())
                renderDebugInfo(component);
        }
    }

    ea::vector<Node*> debugNodes;
    scene->GetNodesWithTag(debugNodes, "DebugInfoAlways");
    for (Node* node : debugNodes)
    {
        if (selection.contains(WeakPtr<Node>(node)))
            continue;

        for (auto& component: node->GetComponents())
            renderDebugInfo(component);
    }

    for (auto& component : selectedComponents_)
    {
        if (component)
            renderDebugInfo(component);
    }
}

void SceneTab::RenderViewManipulator(ImRect rect)
{
    if (GetScene()->GetComponent<EditorSceneSettings>()->GetCamera2D())
        return;

    const ImVec2 size{128, 128};
    const ImVec2 pos{rect.Max.x - 128, rect.Min.y};
    ui::SetCursorScreenPos(pos);
    ui::InvisibleButton("##view-manipulator", size);
    if (ui::ItemMouseActivation(MOUSEB_LEFT, ImGuiItemMouseActivation_Dragging))
        ui::SetMouseCursor(ImGuiMouseCursor_None);
    else if (ui::WasItemActive())
        ui::SetMouseCursor(ImGuiMouseCursor_None);

    Camera* camera = GetCamera();
    Node* cameraNode = camera->GetNode();
    if (ui::IsItemClicked(MOUSEB_LEFT))
    {
        rotateAroundDistance_ = 1;
        if (!selectedNodes_.empty())
        {
            Vector3 center;
            for (Node* node : selectedNodes_)
            {
                StaticModel* model = node->GetComponent<StaticModel>();
                if (model == nullptr)
                    model = node->GetComponent<AnimatedModel>();
                if (model)
                    center += model->GetWorldBoundingBox().Center();
                else
                    center += node->GetWorldPosition();
            }
            center /= selectedNodes_.size();

            // Smooth look-at.
            Quaternion newRotation;
            Vector3 lookDir = center - cameraNode->GetWorldPosition();
            if (!lookDir.Equals(Vector3::ZERO) && newRotation.FromLookRotation(lookDir, Vector3::UP))
            {
                // TODO: Ease-in / ease-out may look good here, but ValueAnimation does not support them.
                SharedPtr<ValueAnimation> rotationAnimation = context_->CreateObject<ValueAnimation>();
                rotationAnimation->SetKeyFrame(0.0f, cameraNode->GetRotation());
                rotationAnimation->SetKeyFrame(0.1f, newRotation);
                cameraNode->SetAttributeAnimation("Rotation", rotationAnimation, WM_ONCE, 1);
                rotateAroundDistance_ = Max((center - cameraNode->GetWorldPosition()).Length(), 1);
                cameraNode->SetEnabled(true);
            }
        }
    }

    // TODO: Add a limit for vertical rotation angles. Going too high or too low makes camera go crazy.
    // https://github.com/CedricGuillemet/ImGuizmo/issues/114
    Matrix4 view = camera->GetView().ToMatrix4();
    view = (inversionMatrix * view * inversionMatrix).Transpose();
    ImGuizmo::ViewManipulate(&view.m00_, rotateAroundDistance_, pos, size, 0);
    view = inversionMatrix * view.Transpose() * inversionMatrix;

    if (cameraNode->GetAttributeAnimation("Rotation") == nullptr)
        cameraNode->SetWorldTransform(Matrix3x4(view.Inverse()));
    else
    {
        // Editor scene is manually updated therefore we must fire this event ourselves.
        using namespace AttributeAnimationUpdate;
        VariantMap& args = GetEventDataMap();
        args[P_TIMESTEP] = GetSubsystem<Time>()->GetTimeStep();
        args[P_SCENE] = GetScene();
        cameraNode->OnEvent(GetScene(), E_ATTRIBUTEANIMATIONUPDATE, args);
    }

    // Eat click event so selection does not change.
    if (ui::IsItemClicked(MOUSEB_LEFT))
        isClickedLeft_ = isClickedRight_ = isViewportActive_ = false;
}

void SceneTab::Close()
{
    UndoTrackGuard noTrack(undo_, false);

    SceneManager* manager = GetSubsystem<SceneManager>();
    manager->UnloadScene(GetScene());

    BaseClassName::Close();

    GetSubsystem<Editor>()->UpdateWindowTitle();
}

}
