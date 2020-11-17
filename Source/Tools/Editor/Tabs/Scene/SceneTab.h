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


#include <Urho3D/IO/BinaryArchive.h>
#include <Urho3D/Scene/SceneManager.h>
#include <Toolbox/SystemUI/AttributeInspector.h>
#include <Toolbox/SystemUI/Gizmo.h>
#include <Toolbox/Graphics/SceneView.h>
#include "Tabs/BaseResourceTab.h"
#include "Tabs/Scene/SceneClipboard.h"


namespace Urho3D
{

class EditorSceneSettings;

struct SceneState
{
    void Save(Scene* scene)
    {
        sceneState_.Clear();
        BinaryOutputArchive sceneArchive(scene->GetContext(), sceneState_);
        scene->Serialize(sceneArchive);
    }

    void Load(Scene* scene)
    {
        sceneState_.Seek(0);
        BinaryInputArchive sceneArchive(scene->GetContext(), sceneState_);
        scene->Serialize(sceneArchive);
        scene->GetContext()->GetSubsystem<UI>()->Clear();
        sceneState_.Clear();
        scene->GetSubsystem<SceneManager>()->SetActiveScene(scene);
    }

    ///
    VectorBuffer sceneState_;
};

class SceneTab : public BaseResourceTab, public IHierarchyProvider
{
    URHO3D_OBJECT(SceneTab, BaseResourceTab);
public:
    /// Selection mode.
    enum class SelectionMode
    {
        /// Include items in selection.
        Add,
        /// Exclude items from selection.
        Subtract,
        /// Toggle item selection status.
        Toggle,
    };

    /// Construct.
    explicit SceneTab(Context* context);
    /// Destruct.
    ~SceneTab() override;
    /// Render scene hierarchy window starting from the root node (scene).
    void RenderHierarchy() override;
    /// Render buttons which customize gizmo behavior.
    void RenderToolbarButtons() override;
    /// Load scene from xml or json file.
    bool LoadResource(const ea::string& resourcePath) override;
    /// Save scene to a resource file.
    bool SaveResource() override;
    ///
    StringHash GetResourceType() override { return XMLFile::GetTypeStatic(); };
    /// Called when tab focused.
    void OnFocused() override;
    /// Modify current scene selection.
    void ModifySelection(const ea::vector<Node*>& nodes, const ea::vector<Component*>& components, SelectionMode mode);
    /// Modify current scene selection.
    void ModifySelection(const ea::vector<Component*>& components, SelectionMode mode)              { ModifySelection({}, components, mode); }
    /// Modify current scene selection.
    void ModifySelection(const ea::vector<Node*>& nodes, SelectionMode mode)                        { ModifySelection(nodes, {}, mode); }
    /// Select nodes and components.
    void Select(const ea::vector<Node*>& nodes, const ea::vector<Component*>& components)           { ModifySelection(nodes, components, SelectionMode::Add); }
    /// Select components.
    void Select(const ea::vector<Component*>& components)                                           { ModifySelection({}, components, SelectionMode::Add); }
    /// Select nodes.
    void Select(const ea::vector<Node*>& nodes)                                                     { ModifySelection(nodes, {}, SelectionMode::Add); }
    /// Select node and component.
    void Select(Node* node, Component* component)                                                   { ModifySelection({node}, {component}, SelectionMode::Add); }
    /// Select component.
    void Select(Component* component)                                                               { ModifySelection({}, {component}, SelectionMode::Add); }
    /// Select node.
    void Select(Node* node)                                                                         { ModifySelection({node}, {}, SelectionMode::Add); }
    /// Unselect nodes and components.
    void Unselect(const ea::vector<Node*>& nodes, const ea::vector<Component*>& components)         { ModifySelection(nodes, components, SelectionMode::Subtract); }
    /// Unselect components.
    void Unselect(const ea::vector<Component*>& components)                                         { ModifySelection({}, components, SelectionMode::Subtract); }
    /// Unselect nodes.
    void Unselect(const ea::vector<Node*>& nodes)                                                   { ModifySelection(nodes, {}, SelectionMode::Subtract); }
    /// Unselect node and component.
    void Unselect(Node* node, Component* component)                                                 { ModifySelection({node}, {component}, SelectionMode::Subtract); }
    /// Unselect component.
    void Unselect(Component* component)                                                             { ModifySelection({}, {component}, SelectionMode::Subtract); }
    /// Unselect node.
    void Unselect(Node* node)                                                                       { ModifySelection({node}, {}, SelectionMode::Subtract); }
    /// Toggle selection of nodes and components.
    void ToggleSelection(const ea::vector<Node*>& nodes, const ea::vector<Component*>& components)  { ModifySelection(nodes, components, SelectionMode::Toggle); }
    /// Toggle selection of components.
    void ToggleSelection(const ea::vector<Component*>& components)                                  { ModifySelection({}, components, SelectionMode::Toggle); }
    /// Toggle selection of nodes.
    void ToggleSelection(const ea::vector<Node*>& nodes)                                            { ModifySelection(nodes, {}, SelectionMode::Toggle); }
    /// Toggle selection of node and component.
    void ToggleSelection(Node* node, Component* component)                                          { ModifySelection({node}, {component}, SelectionMode::Toggle); }
    /// Toggle selection of component.
    void ToggleSelection(Component* component)                                                      { ModifySelection({}, {component}, SelectionMode::Toggle); }
    /// Toggle selection of node.
    void ToggleSelection(Node* node)                                                                { ModifySelection({node}, {}, SelectionMode::Toggle); }
    /// Clear any user selection tracked by this tab.
    void ClearSelection() override;
    /// Serialize current user selection into a buffer and return it.
    bool SerializeSelection(Archive& archive) override;
    /// Return true if node is selected.
    bool IsSelected(Node* node) const;
    /// Return true if component is selected.
    bool IsSelected(Component* component) const;
    /// Removes component if it was selected in inspector, otherwise removes selected scene nodes.
    void RemoveSelection();
    /// Return scene displayed in the tab viewport.
    Scene* GetScene();
    /// Returns editor viewport.
    Viewport* GetViewport() { return viewport_; }
    /// Serialize scene to binary buffer.
    void SaveState(SceneState& destination);
    /// Unserialize scene from binary buffer.
    void RestoreState(SceneState& source);
    /// Returns editor viewport camera.
    Camera* GetCamera();
    /// Closes current tab and unloads it's contents from memory.
    void Close() override;

protected:
    /// Render tab context menu.
    virtual void OnRenderContextMenu();
    /// Render scene hierarchy window starting from specified node.
    void RenderNodeTree(Node* node);
    /// Called when node selection changes.
    void OnNodeSelectionChanged();
    /// Render content of the tab window.
    bool RenderWindowContent() override;
    /// Manually updates scene.
    void OnUpdate(VariantMap& args);
    /// Render context menu of a scene node.
    void RenderNodeContextMenu();
    /// Inserts extra editor objects for representing some components.
    void OnComponentAdded(VariantMap& args);
    /// Removes extra editor objects that were used for representing some components.
    void OnComponentRemoved(VariantMap& args);
    ///
    void OnTemporaryChanged(VariantMap& args);
    ///
    void OnSceneActivated(VariantMap& args);
    ///
    void OnEditorProjectClosing();
    ///
    void AddComponentIcon(Component* component);
    ///
    void RemoveComponentIcon(Component* component);
    /// Add or remove camera preview.
    void UpdateCameras();
    ///
    void CopySelection();
    /// Paste clipboard contents into a parent of first selected node.
    void PasteNextToSelection();
    /// Paste clipboard contents into a selected node.
    void PasteIntoSelection();
    /// Paste components into selection or nodes into parent if first selected node.
    void PasteIntuitive();
    ///
    void RenderDebugInfo();
    /// Editor camera rotation guide at the top-right corner.
    void RenderViewManipulator(ImRect rect);

    /// Rectangle dimensions that are rendered by this view.
    IntRect rect_;
    /// Texture to which scene is rendered.
    SharedPtr<Texture2D> texture_;
    /// Viewport which defines rendering area.
    SharedPtr<Viewport> viewport_;
    /// Gizmo used for manipulating scene elements.
    Gizmo gizmo_;
    /// Current selected nodes displayed in inspector.
    ea::hash_set<WeakPtr<Node>> selectedNodes_;
    /// Current selected component displayed in inspector.
    ea::hash_set<WeakPtr<Component>> selectedComponents_;
    /// Flag indicating that mouse is hovering scene viewport.
    bool isViewportActive_ = false;
    /// Mouse button that clicked viewport.
    ImGuiMouseButton mouseButtonClickedViewport_ = ImGuiMouseButton_COUNT;
    /// Flag indicating that left mouse button was clicked on scene viewport.
    bool isClickedLeft_ = false;
    /// Flag indicating that right mouse button was clicked on scene viewport.
    bool isClickedRight_ = false;
    /// Nodes whose entries in hierarchy tree should be opened on next frame.
    ea::vector<Node*> openHierarchyNodes_;
    /// Node to scroll to on next frame.
    WeakPtr<Node> scrollTo_;
    /// Selected camera preview texture.
    SharedPtr<Texture2D> cameraPreviewTexture_;
    /// Selected camera preview viewport.
    SharedPtr<Viewport> cameraPreviewViewport_;
    /// Utility for copying and pasting scene nodes.
    SceneClipboard clipboard_;
    /// List of node IDs that are saved when scene state is saved. Node selection will be restored using these.
    ea::vector<unsigned> savedNodeSelection_;
    /// List of component IDs that are saved when scene state is saved. Component selection will be restored using these.
    ea::vector<unsigned> savedComponentSelection_;
    ///
    bool debugHudVisible_ = false;
    /// Rectangle encompassing all selected nodes.
    ImRect selectionRect_{};
    /// Frame on which range selection will be performed.
    int performRangeSelectionFrame_ = -1;
    /// We have to use our own because drawlist splitter may be used by other widgets.
    ImDrawListSplitter viewportSplitter_{};
    /// Distance from the camera that manipulator will rotate around.
    float rotateAroundDistance_ = 1;
};

};
