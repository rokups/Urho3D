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

#include "../Precompiled.h"

#include "../Core/Context.h"
#include "../Core/Profiler.h"
#include "../IO/Archive.h"
#include "../IO/ArchiveSerialization.h"
#include "../IO/Log.h"
#include "../IO/MemoryBuffer.h"
#include "../Resource/XMLFile.h"
#include "../Resource/JSONFile.h"
#include "../Scene/Component.h"
#include "../Scene/ObjectAnimation.h"
#include "../Scene/ReplicationState.h"
#include "../Scene/Scene.h"
#include "../Scene/SceneEvents.h"
#include "../Scene/SmoothedTransform.h"
#include "../Scene/UnknownComponent.h"

#include "../DebugNew.h"

#ifdef _MSC_VER
#pragma warning(disable:6293)
#endif

namespace Urho3D
{

Node::Node(Context* context) :
    Animatable(context),
    worldTransform_(Matrix3x4::IDENTITY),
    dirty_(false),
    enabled_(true),
    enabledPrev_(true),
    networkUpdate_(false),
    parent_(nullptr),
    scene_(nullptr),
    id_(0),
    position_(Vector3::ZERO),
    rotation_(Quaternion::IDENTITY),
    scale_(Vector3::ONE),
    worldRotation_(Quaternion::IDENTITY)
{
    impl_ = ea::make_unique<NodeImpl>();
    impl_->owner_ = nullptr;
}

Node::~Node()
{
    RemoveAllChildren();
    RemoveAllComponents();

    // Remove from the scene
    if (scene_)
        scene_->NodeRemoved(this);
}

void Node::RegisterObject(Context* context)
{
    context->RegisterFactory<Node>();

    URHO3D_ACCESSOR_ATTRIBUTE("Is Enabled", IsEnabled, SetEnabled, bool, true, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Name", GetName, SetName, ea::string, EMPTY_STRING, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Tags", GetTags, SetTags, StringVector, Variant::emptyStringVector, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Position", GetPosition, SetPosition, Vector3, Vector3::ZERO, AM_FILE);
    URHO3D_ACCESSOR_ATTRIBUTE("Rotation", GetRotation, SetRotation, Quaternion, Quaternion::IDENTITY, AM_FILE);
    URHO3D_ACCESSOR_ATTRIBUTE("Scale", GetScale, SetScale, Vector3, Vector3::ONE, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Variables", VariantMap, vars_, Variant::emptyVariantMap, AM_FILE); // Network replication of vars uses custom data
    URHO3D_ACCESSOR_ATTRIBUTE("Network Position", GetNetPositionAttr, SetNetPositionAttr, Vector3, Vector3::ZERO,
        AM_NET | AM_LATESTDATA | AM_NOEDIT);
    URHO3D_ACCESSOR_ATTRIBUTE("Network Rotation", GetNetRotationAttr, SetNetRotationAttr, ea::vector<unsigned char>, Variant::emptyBuffer,
        AM_NET | AM_LATESTDATA | AM_NOEDIT);
    URHO3D_ACCESSOR_ATTRIBUTE("Network Parent Node", GetNetParentAttr, SetNetParentAttr, ea::vector<unsigned char>, Variant::emptyBuffer,
        AM_NET | AM_NOEDIT);
}

bool Node::Serialize(Archive& archive)
{
    if (ArchiveBlock block = archive.OpenUnorderedBlock("node"))
    {
        if (archive.IsInput())
        {
            SceneResolver resolver;

            // Load this node ID for resolver
            unsigned nodeID{};
            if (!SerializeValue(archive, "id", id_))
                return false;
            resolver.AddNode(nodeID, this);

            // Load node content
            if (!Serialize(archive, block, &resolver))
                return false;

            // Resolve IDs and apply attributes
            resolver.Resolve();
            ApplyAttributes();
        }
        else
        {
            // Save node ID and content
            SerializeValue(archive, "id", id_);
            Serialize(archive, block, nullptr);
        }
        return true;
    }
    return false;
}

bool Node::Serialize(Archive& archive, ArchiveBlock& block, SceneResolver* resolver,
    bool serializeChildren /*= true*/, bool rewriteIDs /*= false*/, CreateMode mode /*= REPLICATED*/)
{
    // Resolver must be present if loading
    const bool loading = archive.IsInput();
    assert(loading == !!resolver);

    // Remove all children and components first in case this is not a fresh load
    if (loading)
    {
        RemoveAllChildren();
        RemoveAllComponents();
    }

    // Serialize base class
    if (!Animatable::Serialize(archive, block))
        return false;

    // Serialize components
    const unsigned numComponentsToWrite = loading ? 0 : GetNumPersistentComponents();
    const bool componentsSerialized = SerializeCustomVector(archive, ArchiveBlockType::Array, "components", numComponentsToWrite, components_,
        [&](unsigned /*index*/, SharedPtr<Component> component, bool loading)
    {
        assert(loading || component);

        // Skip temporary components
        if (component && component->IsTemporary())
            return true;

        // Serialize component
        if (ArchiveBlock componentBlock = archive.OpenSafeUnorderedBlock("component"))
        {
            // Serialize component ID and type
            unsigned componentID = component ? component->GetID() : 0;
            StringHash componentType = component ? component->GetType() : StringHash{};
            const ea::string& componentTypeName = component ? component->GetTypeName() : EMPTY_STRING;
            SerializeValue(archive, "id", componentID);
            SerializeStringHash(archive, "type", componentType, componentTypeName);

            // Create component if loading
            if (loading)
            {
                const bool isReplicated = mode == REPLICATED && Scene::IsReplicatedID(componentID);
                component = SafeCreateComponent(EMPTY_STRING, componentType, isReplicated ? REPLICATED : LOCAL, componentID);

                // Add component to resolver
                resolver->AddComponent(componentID, component);
            }

            // Serialize component.
            component->Serialize(archive, componentBlock);
            return true;
        }
        return false;
    });

    if (!componentsSerialized)
        return false;

    // Skip children
    if (!serializeChildren)
        return true;

    // Serialize children
    const unsigned numChildrenToWrite = loading ? 0 : GetNumPersistentChildren();
    const bool childrenSerialized = SerializeCustomVector(archive, ArchiveBlockType::Array, "children", numChildrenToWrite, children_,
        [&](unsigned /*index*/, SharedPtr<Node> child, bool loading)
    {
        assert(loading || child);

        // Skip temporary children
        if (child && child->IsTemporary())
            return true;

        // Serialize child
        if (ArchiveBlock childBlock = archive.OpenUnorderedBlock("child"))
        {
            // Serialize node ID
            unsigned nodeID = child ? child->GetID() : 0;
            SerializeValue(archive, "id", nodeID);

            // Create child if loading
            if (loading)
            {
                const bool isReplicated = mode == REPLICATED && Scene::IsReplicatedID(nodeID);
                child = CreateChild(rewriteIDs ? 0 : nodeID, isReplicated ? REPLICATED : LOCAL);

                // Add child node to resolver
                resolver->AddNode(nodeID, child);
            }

            // Serialize child
            if (!child->Serialize(archive, childBlock, resolver, serializeChildren, rewriteIDs, mode))
                return false;

            return true;
        }

        return false;
    });

    if (!childrenSerialized)
        return false;

    return true;
}

bool Node::Load(Deserializer& source)
{
    SceneResolver resolver;

    // Read own ID. Will not be applied, only stored for resolving possible references
    unsigned nodeID = source.ReadUInt();
    resolver.AddNode(nodeID, this);

    // Read attributes, components and child nodes
    bool success = Load(source, resolver);
    if (success)
    {
        resolver.Resolve();
        ApplyAttributes();
    }

    return success;
}

bool Node::Save(Serializer& dest) const
{
    // Write node ID
    if (!dest.WriteUInt(id_))
        return false;

    // Write attributes
    if (!Animatable::Save(dest))
        return false;

    // Write components
    dest.WriteVLE(GetNumPersistentComponents());
    for (unsigned i = 0; i < components_.size(); ++i)
    {
        Component* component = components_[i];
        if (component->IsTemporary())
            continue;

        // Create a separate buffer to be able to skip failing components during deserialization
        VectorBuffer compBuffer;
        if (!component->Save(compBuffer))
            return false;
        dest.WriteVLE(compBuffer.GetSize());
        dest.Write(compBuffer.GetData(), compBuffer.GetSize());
    }

    // Write child nodes
    dest.WriteVLE(GetNumPersistentChildren());
    for (unsigned i = 0; i < children_.size(); ++i)
    {
        Node* node = children_[i];
        if (node->IsTemporary())
            continue;

        if (!node->Save(dest))
            return false;
    }

    return true;
}

bool Node::LoadXML(const XMLElement& source)
{
    SceneResolver resolver;

    // Read own ID. Will not be applied, only stored for resolving possible references
    unsigned nodeID = source.GetUInt("id");
    resolver.AddNode(nodeID, this);

    // Read attributes, components and child nodes
    bool success = LoadXML(source, resolver);
    if (success)
    {
        resolver.Resolve();
        ApplyAttributes();
    }

    return success;
}

bool Node::LoadJSON(const JSONValue& source)
{
    SceneResolver resolver;

    // Read own ID. Will not be applied, only stored for resolving possible references
    unsigned nodeID = source.Get("id").GetUInt();
    resolver.AddNode(nodeID, this);

    // Read attributes, components and child nodes
    bool success = LoadJSON(source, resolver);
    if (success)
    {
        resolver.Resolve();
        ApplyAttributes();
    }

    return success;
}

bool Node::SaveXML(XMLElement& dest) const
{
    // Write node ID
    if (!dest.SetUInt("id", id_))
        return false;

    // Write attributes
    if (!Animatable::SaveXML(dest))
        return false;

    // Write components
    for (unsigned i = 0; i < components_.size(); ++i)
    {
        Component* component = components_[i];
        if (component->IsTemporary())
            continue;

        XMLElement compElem = dest.CreateChild("component");
        if (!component->SaveXML(compElem))
            return false;
    }

    // Write child nodes
    for (unsigned i = 0; i < children_.size(); ++i)
    {
        Node* node = children_[i];
        if (node->IsTemporary())
            continue;

        XMLElement childElem = dest.CreateChild("node");
        if (!node->SaveXML(childElem))
            return false;
    }

    return true;
}

bool Node::SaveJSON(JSONValue& dest) const
{
    // Write node ID
    dest.Set("id", id_);

    // Write attributes
    if (!Animatable::SaveJSON(dest))
        return false;

    // Write components
    JSONArray componentsArray;
    componentsArray.reserve(components_.size());
    for (unsigned i = 0; i < components_.size(); ++i)
    {
        Component* component = components_[i];
        if (component->IsTemporary())
            continue;

        JSONValue compVal;
        if (!component->SaveJSON(compVal))
            return false;
        componentsArray.push_back(compVal);
    }
    dest.Set("components", componentsArray);

    // Write child nodes
    JSONArray childrenArray;
    childrenArray.reserve(children_.size());
    for (unsigned i = 0; i < children_.size(); ++i)
    {
        Node* node = children_[i];
        if (node->IsTemporary())
            continue;

        JSONValue childVal;
        if (!node->SaveJSON(childVal))
            return false;
        childrenArray.push_back(childVal);
    }
    dest.Set("children", childrenArray);

    return true;
}

void Node::ApplyAttributes()
{
    for (unsigned i = 0; i < components_.size(); ++i)
        components_[i]->ApplyAttributes();

    for (unsigned i = 0; i < children_.size(); ++i)
        children_[i]->ApplyAttributes();
}

void Node::MarkNetworkUpdate()
{
    if (!networkUpdate_ && scene_ && IsReplicated())
    {
        scene_->MarkNetworkUpdate(this);
        networkUpdate_ = true;
    }
}

void Node::AddReplicationState(NodeReplicationState* state)
{
    if (!networkState_)
        AllocateNetworkState();

    networkState_->replicationStates_.push_back(state);
}

bool Node::SaveXML(Serializer& dest, const ea::string& indentation) const
{
    SharedPtr<XMLFile> xml(context_->CreateObject<XMLFile>());
    XMLElement rootElem = xml->CreateRoot("node");
    if (!SaveXML(rootElem))
        return false;

    return xml->Save(dest, indentation);
}

bool Node::SaveJSON(Serializer& dest, const ea::string& indentation) const
{
    SharedPtr<JSONFile> json(context_->CreateObject<JSONFile>());
    JSONValue& rootElem = json->GetRoot();

    if (!SaveJSON(rootElem))
        return false;

    return json->Save(dest, indentation);
}

void Node::SetName(const ea::string& name)
{
    if (name != impl_->name_)
    {
        impl_->name_ = name;
        impl_->nameHash_ = name;

        MarkNetworkUpdate();

        // Send change event
        if (scene_)
        {
            using namespace NodeNameChanged;

            VariantMap& eventData = GetEventDataMap();
            eventData[P_SCENE] = scene_;
            eventData[P_NODE] = this;

            scene_->SendEvent(E_NODENAMECHANGED, eventData);
        }
    }
}

void Node::SetTags(const StringVector& tags)
{
    RemoveAllTags();
    AddTags(tags);
    // MarkNetworkUpdate() already called in RemoveAllTags() / AddTags()
}

void Node::AddTag(const ea::string& tag)
{
    // Check if tag empty or already added
    if (tag.empty() || HasTag(tag))
        return;

    // Add tag
    impl_->tags_.push_back(tag);

    // Cache
    if (scene_)
    {
        scene_->NodeTagAdded(this, tag);

        // Send event
        using namespace NodeTagAdded;
        VariantMap& eventData = GetEventDataMap();
        eventData[P_SCENE] = scene_;
        eventData[P_NODE] = this;
        eventData[P_TAG] = tag;
        scene_->SendEvent(E_NODETAGADDED, eventData);
    }
    // Sync
    MarkNetworkUpdate();
}

void Node::AddTags(const ea::string& tags, char separator)
{
    StringVector tagVector = tags.split(separator);
    AddTags(tagVector);
}

void Node::AddTags(const StringVector& tags)
{
    // This is OK, as MarkNetworkUpdate() early-outs when called multiple times
    for (unsigned i = 0; i < tags.size(); ++i)
        AddTag(tags[i]);
}

bool Node::RemoveTag(const ea::string& tag)
{
    auto it = impl_->tags_.find(tag);

    // Nothing to do
    if (it == impl_->tags_.end())
        return false;

    impl_->tags_.erase(it);

    // Scene cache update
    if (scene_)
    {
        scene_->NodeTagRemoved(this, tag);
        // Send event
        using namespace NodeTagRemoved;
        VariantMap& eventData = GetEventDataMap();
        eventData[P_SCENE] = scene_;
        eventData[P_NODE] = this;
        eventData[P_TAG] = tag;
        scene_->SendEvent(E_NODETAGREMOVED, eventData);
    }

    // Sync
    MarkNetworkUpdate();
    return true;
}

void Node::RemoveAllTags()
{
    // Clear old scene cache
    if (scene_)
    {
        for (unsigned i = 0; i < impl_->tags_.size(); ++i)
        {
            scene_->NodeTagRemoved(this, impl_->tags_[i]);

            // Send event
            using namespace NodeTagRemoved;
            VariantMap& eventData = GetEventDataMap();
            eventData[P_SCENE] = scene_;
            eventData[P_NODE] = this;
            eventData[P_TAG] = impl_->tags_[i];
            scene_->SendEvent(E_NODETAGREMOVED, eventData);
        }
    }

    impl_->tags_.clear();

    // Sync
    MarkNetworkUpdate();
}

void Node::SetPosition(const Vector3& position)
{
    position_ = position;
    MarkDirty();

    MarkNetworkUpdate();
}

void Node::SetRotation(const Quaternion& rotation)
{
    rotation_ = rotation;
    MarkDirty();

    MarkNetworkUpdate();
}

void Node::SetDirection(const Vector3& direction)
{
    SetRotation(Quaternion(Vector3::FORWARD, direction));
}

void Node::SetScale(float scale)
{
    SetScale(Vector3(scale, scale, scale));
}

void Node::SetScale(const Vector3& scale)
{
    scale_ = scale;
    // Prevent exact zero scale e.g. from momentary edits as this may cause division by zero
    // when decomposing the world transform matrix
    if (scale_.x_ == 0.0f)
        scale_.x_ = M_EPSILON;
    if (scale_.y_ == 0.0f)
        scale_.y_ = M_EPSILON;
    if (scale_.z_ == 0.0f)
        scale_.z_ = M_EPSILON;

    MarkDirty();
    MarkNetworkUpdate();
}

void Node::SetTransform(const Vector3& position, const Quaternion& rotation)
{
    position_ = position;
    rotation_ = rotation;
    MarkDirty();

    MarkNetworkUpdate();
}

void Node::SetTransform(const Vector3& position, const Quaternion& rotation, float scale)
{
    SetTransform(position, rotation, Vector3(scale, scale, scale));
}

void Node::SetTransform(const Vector3& position, const Quaternion& rotation, const Vector3& scale)
{
    position_ = position;
    rotation_ = rotation;
    scale_ = scale;
    MarkDirty();

    MarkNetworkUpdate();
}

void Node::SetTransform(const Matrix3x4& matrix)
{
    SetTransform(matrix.Translation(), matrix.Rotation(), matrix.Scale());
}

void Node::SetWorldPosition(const Vector3& position)
{
    SetPosition((parent_ == scene_ || !parent_) ? position : parent_->GetWorldTransform().Inverse() * position);
}

void Node::SetWorldRotation(const Quaternion& rotation)
{
    SetRotation((parent_ == scene_ || !parent_) ? rotation : parent_->GetWorldRotation().Inverse() * rotation);
}

void Node::SetWorldDirection(const Vector3& direction)
{
    Vector3 localDirection = (parent_ == scene_ || !parent_) ? direction : parent_->GetWorldRotation().Inverse() * direction;
    SetRotation(Quaternion(Vector3::FORWARD, localDirection));
}

void Node::SetWorldScale(float scale)
{
    SetWorldScale(Vector3(scale, scale, scale));
}

void Node::SetWorldScale(const Vector3& scale)
{
    SetScale((parent_ == scene_ || !parent_) ? scale : scale / parent_->GetWorldScale());
}

void Node::SetWorldTransform(const Vector3& position, const Quaternion& rotation)
{
    SetWorldPosition(position);
    SetWorldRotation(rotation);
}

void Node::SetWorldTransform(const Vector3& position, const Quaternion& rotation, float scale)
{
    SetWorldPosition(position);
    SetWorldRotation(rotation);
    SetWorldScale(scale);
}

void Node::SetWorldTransform(const Vector3& position, const Quaternion& rotation, const Vector3& scale)
{
    SetWorldPosition(position);
    SetWorldRotation(rotation);
    SetWorldScale(scale);
}

void Node::SetWorldTransform(const Matrix3x4& worldTransform)
{
    SetWorldTransform(worldTransform.Translation(), worldTransform.Rotation(), worldTransform.Scale());
}

void Node::Translate(const Vector3& delta, TransformSpace space)
{
    switch (space)
    {
    case TS_LOCAL:
        // Note: local space translation disregards local scale for scale-independent movement speed
        position_ += rotation_ * delta;
        break;

    case TS_PARENT:
        position_ += delta;
        break;

    case TS_WORLD:
        position_ += (parent_ == scene_ || !parent_) ? delta : parent_->GetWorldTransform().Inverse() * Vector4(delta, 0.0f);
        break;
    }

    MarkDirty();

    MarkNetworkUpdate();
}

void Node::Rotate(const Quaternion& delta, TransformSpace space)
{
    switch (space)
    {
    case TS_LOCAL:
        rotation_ = (rotation_ * delta).Normalized();
        break;

    case TS_PARENT:
        rotation_ = (delta * rotation_).Normalized();
        break;

    case TS_WORLD:
        if (parent_ == scene_ || !parent_)
            rotation_ = (delta * rotation_).Normalized();
        else
        {
            Quaternion worldRotation = GetWorldRotation();
            rotation_ = rotation_ * worldRotation.Inverse() * delta * worldRotation;
        }
        break;
    }

    MarkDirty();

    MarkNetworkUpdate();
}

void Node::RotateAround(const Vector3& point, const Quaternion& delta, TransformSpace space)
{
    Vector3 parentSpacePoint;
    Quaternion oldRotation = rotation_;

    switch (space)
    {
    case TS_LOCAL:
        parentSpacePoint = GetTransform() * point;
        rotation_ = (rotation_ * delta).Normalized();
        break;

    case TS_PARENT:
        parentSpacePoint = point;
        rotation_ = (delta * rotation_).Normalized();
        break;

    case TS_WORLD:
        if (parent_ == scene_ || !parent_)
        {
            parentSpacePoint = point;
            rotation_ = (delta * rotation_).Normalized();
        }
        else
        {
            parentSpacePoint = parent_->GetWorldTransform().Inverse() * point;
            Quaternion worldRotation = GetWorldRotation();
            rotation_ = rotation_ * worldRotation.Inverse() * delta * worldRotation;
        }
        break;
    }

    Vector3 oldRelativePos = oldRotation.Inverse() * (position_ - parentSpacePoint);
    position_ = rotation_ * oldRelativePos + parentSpacePoint;

    MarkDirty();

    MarkNetworkUpdate();
}

void Node::Yaw(float angle, TransformSpace space)
{
    Rotate(Quaternion(angle, Vector3::UP), space);
}

void Node::Pitch(float angle, TransformSpace space)
{
    Rotate(Quaternion(angle, Vector3::RIGHT), space);
}

void Node::Roll(float angle, TransformSpace space)
{
    Rotate(Quaternion(angle, Vector3::FORWARD), space);
}

bool Node::LookAt(const Vector3& target, const Vector3& up, TransformSpace space)
{
    Vector3 worldSpaceTarget;

    switch (space)
    {
    case TS_LOCAL:
        worldSpaceTarget = GetWorldTransform() * target;
        break;

    case TS_PARENT:
        worldSpaceTarget = (parent_ == scene_ || !parent_) ? target : parent_->GetWorldTransform() * target;
        break;

    case TS_WORLD:
        worldSpaceTarget = target;
        break;
    }

    Vector3 lookDir = worldSpaceTarget - GetWorldPosition();
    // Check if target is very close, in that case can not reliably calculate lookat direction
    if (lookDir.Equals(Vector3::ZERO))
        return false;
    Quaternion newRotation;
    // Do nothing if setting look rotation failed
    if (!newRotation.FromLookRotation(lookDir, up))
        return false;

    SetWorldRotation(newRotation);
    return true;
}

void Node::Scale(float scale)
{
    Scale(Vector3(scale, scale, scale));
}

void Node::Scale(const Vector3& scale)
{
    scale_ *= scale;
    MarkDirty();

    MarkNetworkUpdate();
}

void Node::SetEnabled(bool enable)
{
    SetEnabled(enable, false, true);
}

void Node::SetDeepEnabled(bool enable)
{
    SetEnabled(enable, true, false);
}

void Node::ResetDeepEnabled()
{
    SetEnabled(enabledPrev_, false, false);

    for (auto i = children_.begin(); i != children_.end(); ++i)
        (*i)->ResetDeepEnabled();
}

void Node::SetEnabledRecursive(bool enable)
{
    SetEnabled(enable, true, true);
}

void Node::SetOwner(Connection* owner)
{
    impl_->owner_ = owner;
}

void Node::MarkDirty()
{
    Node *cur = this;
    for (;;)
    {
        // Precondition:
        // a) whenever a node is marked dirty, all its children are marked dirty as well.
        // b) whenever a node is cleared from being dirty, all its parents must have been
        //    cleared as well.
        // Therefore if we are recursing here to mark this node dirty, and it already was,
        // then all children of this node must also be already dirty, and we don't need to
        // reflag them again.
        if (cur->dirty_)
            return;
        cur->dirty_ = true;

        // Notify listener components first, then mark child nodes
        for (auto i = cur->listeners_.begin(); i !=
            cur->listeners_.end();)
        {
            Component *c = i->Get();
            if (c)
            {
                c->OnMarkedDirty(cur);
                ++i;
            }
            // If listener has expired, erase from list (swap with the last element to avoid O(n^2) behavior)
            else
            {
                *i = cur->listeners_.back();
                cur->listeners_.pop_back();
            }
        }

        // Tail call optimization: Don't recurse to mark the first child dirty, but
        // instead process it in the context of the current function. If there are more
        // than one child, then recurse to the excess children.
        auto i = cur->children_.begin();
        if (i != cur->children_.end())
        {
            Node *next = i->Get();
            for (++i; i != cur->children_.end(); ++i)
                (*i)->MarkDirty();
            cur = next;
        }
        else
            return;
    }
}

Node* Node::CreateChild(const ea::string& name, CreateMode mode, unsigned id, bool temporary)
{
    Node* newNode = CreateChild(id, mode, temporary);
    newNode->SetName(name);
    return newNode;
}

Node* Node::CreateTemporaryChild(const ea::string& name, CreateMode mode, unsigned id)
{
    return CreateChild(name, mode, id, true);
}

void Node::AddChild(Node* node, unsigned index)
{
    // Check for illegal or redundant parent assignment
    if (!node || node == this || node->parent_ == this)
        return;
    // Check for possible cyclic parent assignment
    if (IsChildOf(node))
        return;

    // Keep a shared ptr to the node while transferring
    SharedPtr<Node> nodeShared(node);
    Node* oldParent = node->parent_;
    if (oldParent)
    {
        // If old parent is in different scene, perform the full removal
        if (oldParent->GetScene() != scene_)
            oldParent->RemoveChild(node);
        else
        {
            if (scene_)
            {
                // Otherwise do not remove from the scene during reparenting, just send the necessary change event
                using namespace NodeRemoved;

                VariantMap& eventData = GetEventDataMap();
                eventData[P_SCENE] = scene_;
                eventData[P_PARENT] = oldParent;
                eventData[P_NODE] = node;

                scene_->SendEvent(E_NODEREMOVED, eventData);
            }

            oldParent->children_.erase_first(nodeShared);
        }
    }

    // Add to the child vector, then add to the scene if not added yet
    children_.insert_at(index, nodeShared);
    if (scene_ && node->GetScene() != scene_)
        scene_->NodeAdded(node);

    node->parent_ = this;
    node->MarkDirty();
    node->MarkNetworkUpdate();
    // If the child node has components, also mark network update on them to ensure they have a valid NetworkState
    for (auto i = node->components_.begin(); i !=
        node->components_.end(); ++i)
        (*i)->MarkNetworkUpdate();

    // Send change event
    if (scene_)
    {
        using namespace NodeAdded;

        VariantMap& eventData = GetEventDataMap();
        eventData[P_SCENE] = scene_;
        eventData[P_PARENT] = this;
        eventData[P_NODE] = node;

        scene_->SendEvent(E_NODEADDED, eventData);
    }
}

void Node::RemoveChild(Node* node)
{
    if (!node)
        return;

    for (auto i = children_.begin(); i != children_.end(); ++i)
    {
        if (i->Get() == node)
        {
            RemoveChild(i);
            return;
        }
    }
}

void Node::RemoveAllChildren()
{
    RemoveChildren(true, true, true);
}

void Node::RemoveChildren(bool removeReplicated, bool removeLocal, bool recursive)
{
    unsigned numRemoved = 0;

    for (unsigned i = children_.size() - 1; i < children_.size(); --i)
    {
        bool remove = false;
        Node* childNode = children_[i];

        if (recursive)
            childNode->RemoveChildren(removeReplicated, removeLocal, true);
        if (childNode->IsReplicated() && removeReplicated)
            remove = true;
        else if (!childNode->IsReplicated() && removeLocal)
            remove = true;

        if (remove)
        {
            RemoveChild(children_.begin() + i);
            ++numRemoved;
        }
    }

    // Mark node dirty in all replication states
    if (numRemoved)
        MarkReplicationDirty();
}

Component* Node::CreateComponent(StringHash type, CreateMode mode, unsigned id)
{
    // Do not attempt to create replicated components to local nodes, as that may lead to component ID overwrite
    // as replicated components are synced over
    if (mode == REPLICATED && !IsReplicated())
        mode = LOCAL;

    // Check that creation succeeds and that the object in fact is a component
    SharedPtr<Component> newComponent = DynamicCast<Component>(context_->CreateObject(type));
    if (!newComponent)
    {
        URHO3D_LOGERROR("Could not create unknown component type " + type.ToString());
        return nullptr;
    }

    AddComponent(newComponent, id, mode);
    return newComponent;
}

Component* Node::GetOrCreateComponent(StringHash type, CreateMode mode, unsigned id)
{
    Component* oldComponent = GetComponent(type);
    if (oldComponent)
        return oldComponent;
    else
        return CreateComponent(type, mode, id);
}

Component* Node::CloneComponent(Component* component, unsigned id)
{
    if (!component)
    {
        URHO3D_LOGERROR("Null source component given for CloneComponent");
        return nullptr;
    }

    return CloneComponent(component, component->IsReplicated() ? REPLICATED : LOCAL, id);
}

Component* Node::CloneComponent(Component* component, CreateMode mode, unsigned id)
{
    if (!component)
    {
        URHO3D_LOGERROR("Null source component given for CloneComponent");
        return nullptr;
    }

    Component* cloneComponent = SafeCreateComponent(component->GetTypeName(), component->GetType(), mode, 0);
    if (!cloneComponent)
    {
        URHO3D_LOGERROR("Could not clone component " + component->GetTypeName());
        return nullptr;
    }

    const ea::vector<AttributeInfo>* compAttributes = component->GetAttributes();
    const ea::vector<AttributeInfo>* cloneAttributes = cloneComponent->GetAttributes();

    if (compAttributes)
    {
        for (unsigned i = 0; i < compAttributes->size() && i < cloneAttributes->size(); ++i)
        {
            const AttributeInfo& attr = compAttributes->at(i);
            const AttributeInfo& cloneAttr = cloneAttributes->at(i);
            if (attr.mode_ & AM_FILE)
            {
                Variant value;
                component->OnGetAttribute(attr, value);
                // Note: when eg. a ScriptInstance component is cloned, its script object attributes are unique and therefore we
                // can not simply refer to the source component's AttributeInfo
                cloneComponent->OnSetAttribute(cloneAttr, value);
            }
        }
        cloneComponent->ApplyAttributes();
    }

    if (scene_)
    {
        using namespace ComponentCloned;

        VariantMap& eventData = GetEventDataMap();
        eventData[P_SCENE] = scene_;
        eventData[P_COMPONENT] = component;
        eventData[P_CLONECOMPONENT] = cloneComponent;

        scene_->SendEvent(E_COMPONENTCLONED, eventData);
    }

    return cloneComponent;
}

void Node::RemoveComponent(Component* component)
{
    for (auto i = components_.begin(); i != components_.end(); ++i)
    {
        if (i->Get() == component)
        {
            RemoveComponent(i);

            // Mark node dirty in all replication states
            MarkReplicationDirty();
            return;
        }
    }
}

void Node::RemoveComponent(StringHash type)
{
    for (auto i = components_.begin(); i != components_.end(); ++i)
    {
        if ((*i)->GetType() == type)
        {
            RemoveComponent(i);

            // Mark node dirty in all replication states
            MarkReplicationDirty();
            return;
        }
    }
}

void Node::RemoveComponents(bool removeReplicated, bool removeLocal)
{
    unsigned numRemoved = 0;

    for (unsigned i = components_.size() - 1; i < components_.size(); --i)
    {
        bool remove = false;
        Component* component = components_[i];

        if (component->IsReplicated() && removeReplicated)
            remove = true;
        else if (!component->IsReplicated() && removeLocal)
            remove = true;

        if (remove)
        {
            RemoveComponent(components_.begin() + i);
            ++numRemoved;
        }
    }

    // Mark node dirty in all replication states
    if (numRemoved)
        MarkReplicationDirty();
}

void Node::RemoveComponents(StringHash type)
{
    unsigned numRemoved = 0;

    for (unsigned i = components_.size() - 1; i < components_.size(); --i)
    {
        if (components_[i]->GetType() == type)
        {
            RemoveComponent(components_.begin() + i);
            ++numRemoved;
        }
    }

    // Mark node dirty in all replication states
    if (numRemoved)
        MarkReplicationDirty();
}

void Node::RemoveAllComponents()
{
    RemoveComponents(true, true);
}

void Node::ReorderComponent(Component* component, unsigned index)
{
    if (!component || component->GetNode() != this)
        return;

    for (auto i = components_.begin(); i != components_.end(); ++i)
    {
        if (i->Get() == component)
        {
            // Need shared ptr to insert. Also, prevent destruction when removing first
            SharedPtr<Component> componentShared(component);
            components_.erase(i);
            components_.insert_at(index, componentShared);
            return;
        }
    }
}

Node* Node::Clone(CreateMode mode)
{
    // The scene itself can not be cloned
    if (this == scene_ || !parent_)
    {
        URHO3D_LOGERROR("Can not clone node without a parent");
        return nullptr;
    }

    URHO3D_PROFILE("CloneNode");

    SceneResolver resolver;
    Node* clone = CloneRecursive(parent_, resolver, mode);
    resolver.Resolve();
    clone->ApplyAttributes();
    return clone;
}

void Node::Remove()
{
    if (parent_)
        parent_->RemoveChild(this);
}

void Node::SetParent(Node* parent)
{
    if (parent)
    {
        Matrix3x4 oldWorldTransform = GetWorldTransform();

        parent->AddChild(this);

        if (parent != scene_)
        {
            Matrix3x4 newTransform = parent->GetWorldTransform().Inverse() * oldWorldTransform;
            SetTransform(newTransform.Translation(), newTransform.Rotation(), newTransform.Scale());
        }
        else
        {
            // The root node is assumed to have identity transform, so can disregard it
            SetTransform(oldWorldTransform.Translation(), oldWorldTransform.Rotation(), oldWorldTransform.Scale());
        }
    }
}

void Node::SetVar(StringHash key, const Variant& value)
{
    vars_[key] = value;
    MarkNetworkUpdate();
}

void Node::AddListener(Component* component)
{
    if (!component)
        return;

    // Check for not adding twice
    for (auto i = listeners_.begin(); i != listeners_.end(); ++i)
    {
        if (i->Get() == component)
            return;
    }

    listeners_.push_back(WeakPtr<Component>(component));
    // If the node is currently dirty, notify immediately
    if (dirty_)
        component->OnMarkedDirty(this);
}

void Node::RemoveListener(Component* component)
{
    for (auto i = listeners_.begin(); i != listeners_.end(); ++i)
    {
        if (i->Get() == component)
        {
            listeners_.erase(i);
            return;
        }
    }
}

Vector3 Node::GetSignedWorldScale() const
{
    if (dirty_)
        UpdateWorldTransform();

    return worldTransform_.SignedScale(worldRotation_.RotationMatrix());
}

Vector3 Node::LocalToWorld(const Vector3& position) const
{
    return GetWorldTransform() * position;
}

Vector3 Node::LocalToWorld(const Vector4& vector) const
{
    return GetWorldTransform() * vector;
}

Vector2 Node::LocalToWorld2D(const Vector2& vector) const
{
    Vector3 result = LocalToWorld(Vector3(vector));
    return Vector2(result.x_, result.y_);
}

Vector3 Node::WorldToLocal(const Vector3& position) const
{
    return GetWorldTransform().Inverse() * position;
}

Vector3 Node::WorldToLocal(const Vector4& vector) const
{
    return GetWorldTransform().Inverse() * vector;
}

Vector2 Node::WorldToLocal2D(const Vector2& vector) const
{
    Vector3 result = WorldToLocal(Vector3(vector));
    return Vector2(result.x_, result.y_);
}

unsigned Node::GetNumChildren(bool recursive) const
{
    if (!recursive)
        return children_.size();
    else
    {
        unsigned allChildren = children_.size();
        for (auto i = children_.begin(); i != children_.end(); ++i)
            allChildren += (*i)->GetNumChildren(true);

        return allChildren;
    }
}

void Node::GetChildren(ea::vector<Node*>& dest, bool recursive) const
{
    dest.clear();

    if (!recursive)
    {
        for (auto i = children_.begin(); i != children_.end(); ++i)
            dest.push_back(i->Get());
    }
    else
        GetChildrenRecursive(dest);
}

ea::vector<Node*> Node::GetChildren(bool recursive) const
{
    ea::vector<Node*> dest;
    GetChildren(dest, recursive);
    return dest;
}

void Node::GetChildrenWithComponent(ea::vector<Node*>& dest, StringHash type, bool recursive) const
{
    dest.clear();

    if (!recursive)
    {
        for (auto i = children_.begin(); i != children_.end(); ++i)
        {
            if ((*i)->HasComponent(type))
                dest.push_back(i->Get());
        }
    }
    else
        GetChildrenWithComponentRecursive(dest, type);
}

ea::vector<Node*> Node::GetChildrenWithComponent(StringHash type, bool recursive) const
{
    ea::vector<Node*> dest;
    GetChildrenWithComponent(dest, type, recursive);
    return dest;
}

void Node::GetChildrenWithTag(ea::vector<Node*>& dest, const ea::string& tag, bool recursive /*= true*/) const
{
    dest.clear();

    if (!recursive)
    {
        for (auto i = children_.begin(); i != children_.end(); ++i)
        {
            if ((*i)->HasTag(tag))
                dest.push_back(i->Get());
        }
    }
    else
        GetChildrenWithTagRecursive(dest, tag);
}

ea::vector<Node*> Node::GetChildrenWithTag(const ea::string& tag, bool recursive) const
{
    ea::vector<Node*> dest;
    GetChildrenWithTag(dest, tag, recursive);
    return dest;
}

unsigned Node::GetChildIndex(const Node* child) const
{
    auto iter = ea::find(children_.begin(), children_.end(), child);
    return iter != children_.end() ? static_cast<unsigned>(iter - children_.begin()) : M_MAX_UNSIGNED;
}

Node* Node::GetChild(unsigned index) const
{
    return index < children_.size() ? children_[index] : nullptr;
}

Node* Node::GetChild(const ea::string& name, bool recursive) const
{
    return GetChild(StringHash(name), recursive);
}

Node* Node::GetChild(const char* name, bool recursive) const
{
    return GetChild(StringHash(name), recursive);
}

Node* Node::GetChild(StringHash nameHash, bool recursive) const
{
    for (auto i = children_.begin(); i != children_.end(); ++i)
    {
        if ((*i)->GetNameHash() == nameHash)
            return i->Get();

        if (recursive)
        {
            Node* node = (*i)->GetChild(nameHash, true);
            if (node)
                return node;
        }
    }

    return nullptr;
}

unsigned Node::GetNumNetworkComponents() const
{
    unsigned num = 0;
    for (auto i = components_.begin(); i != components_.end(); ++i)
    {
        if ((*i)->IsReplicated())
            ++num;
    }

    return num;
}

void Node::GetComponents(ea::vector<Component*>& dest, StringHash type, bool recursive) const
{
    dest.clear();

    if (!recursive)
    {
        for (auto i = components_.begin(); i != components_.end(); ++i)
        {
            if ((*i)->GetType() == type)
                dest.push_back(i->Get());
        }
    }
    else
        GetComponentsRecursive(dest, type);
}

unsigned Node::GetComponentIndex(const Component* component) const
{
    auto iter = ea::find(components_.begin(), components_.end(), component);
    return iter != components_.end() ? static_cast<unsigned>(iter - components_.begin()) : M_MAX_UNSIGNED;
}

bool Node::HasComponent(StringHash type) const
{
    for (auto i = components_.begin(); i != components_.end(); ++i)
    {
        if ((*i)->GetType() == type)
            return true;
    }
    return false;
}

bool Node::IsReplicated() const
{
    return Scene::IsReplicatedID(id_);
}

ea::string Node::GetFullNameDebug() const
{
    ea::string fullName = parent_ ? Format("{}/[{}]", parent_->GetFullNameDebug(), parent_->GetChildIndex(this)) : "";
    fullName += impl_->name_.empty() ? GetTypeName() : impl_->name_;
    return fullName;
}

bool Node::HasTag(const ea::string& tag) const
{
    return impl_->tags_.contains(tag);
}

bool Node::IsChildOf(Node* node) const
{
    Node* parent = parent_;
    while (parent)
    {
        if (parent == node)
            return true;
        parent = parent->parent_;
    }
    return false;
}

const Variant& Node::GetVar(StringHash key) const
{
    auto i = vars_.find(key);
    return i != vars_.end() ? i->second : Variant::EMPTY;
}

Component* Node::GetComponent(StringHash type, bool recursive) const
{
    for (auto i = components_.begin(); i != components_.end(); ++i)
    {
        if ((*i)->GetType() == type)
            return i->Get();
    }

    if (recursive)
    {
        for (auto i = children_.begin(); i != children_.end(); ++i)
        {
            Component* component = (*i)->GetComponent(type, true);
            if (component)
                return component;
        }
    }

    return nullptr;
}

Component* Node::GetParentComponent(StringHash type, bool fullTraversal) const
{
    Node* current = GetParent();
    while (current)
    {
        Component* soughtComponent = current->GetComponent(type);
        if (soughtComponent)
            return soughtComponent;

        if (fullTraversal)
            current = current->GetParent();
        else
            break;
    }
    return nullptr;
}

void Node::SetID(unsigned id)
{
    id_ = id;
}

void Node::SetScene(Scene* scene)
{
    scene_ = scene;
}

void Node::ResetScene()
{
    SetID(0);
    SetScene(nullptr);
    SetOwner(nullptr);
}

void Node::SetNetPositionAttr(const Vector3& value)
{
    auto* transform = GetComponent<SmoothedTransform>();
    if (transform)
        transform->SetTargetPosition(value);
    else
        SetPosition(value);
}

void Node::SetNetRotationAttr(const ea::vector<unsigned char>& value)
{
    MemoryBuffer buf(value);
    auto* transform = GetComponent<SmoothedTransform>();
    if (transform)
        transform->SetTargetRotation(buf.ReadPackedQuaternion());
    else
        SetRotation(buf.ReadPackedQuaternion());
}

void Node::SetNetParentAttr(const ea::vector<unsigned char>& value)
{
    Scene* scene = GetScene();
    if (!scene)
        return;

    MemoryBuffer buf(value);
    // If nothing in the buffer, parent is the root node
    if (buf.IsEof())
    {
        scene->AddChild(this);
        return;
    }

    unsigned baseNodeID = buf.ReadNetID();
    Node* baseNode = scene->GetNode(baseNodeID);
    if (!baseNode)
    {
        URHO3D_LOGWARNING("Failed to find parent node " + ea::to_string(baseNodeID));
        return;
    }

    // If buffer contains just an ID, the parent is replicated and we are done
    if (buf.IsEof())
        baseNode->AddChild(this);
    else
    {
        // Else the parent is local and we must find it recursively by name hash
        StringHash nameHash = buf.ReadStringHash();
        Node* parentNode = baseNode->GetChild(nameHash, true);
        if (!parentNode)
            URHO3D_LOGWARNING("Failed to find parent node with name hash " + nameHash.ToString());
        else
            parentNode->AddChild(this);
    }
}

const Vector3& Node::GetNetPositionAttr() const
{
    return position_;
}

const ea::vector<unsigned char>& Node::GetNetRotationAttr() const
{
    impl_->attrBuffer_.Clear();
    impl_->attrBuffer_.WritePackedQuaternion(rotation_);
    return impl_->attrBuffer_.GetBuffer();
}

const ea::vector<unsigned char>& Node::GetNetParentAttr() const
{
    impl_->attrBuffer_.Clear();
    Scene* scene = GetScene();
    if (scene && parent_ && parent_ != scene)
    {
        // If parent is replicated, can write the ID directly
        unsigned parentID = parent_->GetID();
        if (Scene::IsReplicatedID(parentID))
            impl_->attrBuffer_.WriteNetID(parentID);
        else
        {
            // Parent is local: traverse hierarchy to find a non-local base node
            // This iteration always stops due to the scene (root) being non-local
            Node* current = parent_;
            while (!current->IsReplicated())
                current = current->GetParent();

            // Then write the base node ID and the parent's name hash
            impl_->attrBuffer_.WriteNetID(current->GetID());
            impl_->attrBuffer_.WriteStringHash(parent_->GetNameHash());
        }
    }

    return impl_->attrBuffer_.GetBuffer();
}

bool Node::Load(Deserializer& source, SceneResolver& resolver, bool loadChildren, bool rewriteIDs, CreateMode mode)
{
    // Remove all children and components first in case this is not a fresh load
    RemoveAllChildren();
    RemoveAllComponents();

    // ID has been read at the parent level
    if (!Animatable::Load(source))
        return false;

    unsigned numComponents = source.ReadVLE();
    for (unsigned i = 0; i < numComponents; ++i)
    {
        VectorBuffer compBuffer(source, source.ReadVLE());
        StringHash compType = compBuffer.ReadStringHash();
        unsigned compID = compBuffer.ReadUInt();

        Component* newComponent = SafeCreateComponent(EMPTY_STRING, compType,
            (mode == REPLICATED && Scene::IsReplicatedID(compID)) ? REPLICATED : LOCAL, rewriteIDs ? 0 : compID);
        if (newComponent)
        {
            resolver.AddComponent(compID, newComponent);
            // Do not abort if component fails to load, as the component buffer is nested and we can skip to the next
            newComponent->Load(compBuffer);
        }
    }

    if (!loadChildren)
        return true;

    unsigned numChildren = source.ReadVLE();
    for (unsigned i = 0; i < numChildren; ++i)
    {
        unsigned nodeID = source.ReadUInt();
        Node* newNode = CreateChild(rewriteIDs ? 0 : nodeID, (mode == REPLICATED && Scene::IsReplicatedID(nodeID)) ? REPLICATED :
            LOCAL);
        resolver.AddNode(nodeID, newNode);
        if (!newNode->Load(source, resolver, loadChildren, rewriteIDs, mode))
            return false;
    }

    return true;
}

bool Node::LoadXML(const XMLElement& source, SceneResolver& resolver, bool loadChildren, bool rewriteIDs, CreateMode mode)
{
    // Remove all children and components first in case this is not a fresh load
    RemoveAllChildren();
    RemoveAllComponents();

    if (!Animatable::LoadXML(source))
        return false;

    XMLElement compElem = source.GetChild("component");
    while (compElem)
    {
        ea::string typeName = compElem.GetAttribute("type");
        unsigned compID = compElem.GetUInt("id");
        Component* newComponent = SafeCreateComponent(typeName, StringHash(typeName),
            (mode == REPLICATED && Scene::IsReplicatedID(compID)) ? REPLICATED : LOCAL, rewriteIDs ? 0 : compID);
        if (newComponent)
        {
            resolver.AddComponent(compID, newComponent);
            if (!newComponent->LoadXML(compElem))
                return false;
        }

        compElem = compElem.GetNext("component");
    }

    if (!loadChildren)
        return true;

    XMLElement childElem = source.GetChild("node");
    while (childElem)
    {
        unsigned nodeID = childElem.GetUInt("id");
        Node* newNode = CreateChild(rewriteIDs ? 0 : nodeID, (mode == REPLICATED && Scene::IsReplicatedID(nodeID)) ? REPLICATED :
            LOCAL);
        resolver.AddNode(nodeID, newNode);
        if (!newNode->LoadXML(childElem, resolver, loadChildren, rewriteIDs, mode))
            return false;

        childElem = childElem.GetNext("node");
    }

    return true;
}

bool Node::LoadJSON(const JSONValue& source, SceneResolver& resolver, bool loadChildren, bool rewriteIDs, CreateMode mode)
{
    // Remove all children and components first in case this is not a fresh load
    RemoveAllChildren();
    RemoveAllComponents();

    if (!Animatable::LoadJSON(source))
        return false;

    const JSONArray& componentsArray = source.Get("components").GetArray();

    for (unsigned i = 0; i < componentsArray.size(); i++)
    {
        const JSONValue& compVal = componentsArray.at(i);
        ea::string typeName = compVal.Get("type").GetString();
        unsigned compID = compVal.Get("id").GetUInt();
        Component* newComponent = SafeCreateComponent(typeName, StringHash(typeName),
            (mode == REPLICATED && Scene::IsReplicatedID(compID)) ? REPLICATED : LOCAL, rewriteIDs ? 0 : compID);
        if (newComponent)
        {
            resolver.AddComponent(compID, newComponent);
            if (!newComponent->LoadJSON(compVal))
                return false;
        }
    }

    if (!loadChildren)
        return true;

    const JSONArray& childrenArray = source.Get("children").GetArray();
    for (unsigned i = 0; i < childrenArray.size(); i++)
    {
        const JSONValue& childVal = childrenArray.at(i);

        unsigned nodeID = childVal.Get("id").GetUInt();
        Node* newNode = CreateChild(rewriteIDs ? 0 : nodeID, (mode == REPLICATED && Scene::IsReplicatedID(nodeID)) ? REPLICATED :
            LOCAL);
        resolver.AddNode(nodeID, newNode);
        if (!newNode->LoadJSON(childVal, resolver, loadChildren, rewriteIDs, mode))
            return false;
    }

    return true;
}

void Node::PrepareNetworkUpdate()
{
    // Update dependency nodes list first
    impl_->dependencyNodes_.clear();

    // Add the parent node, but if it is local, traverse to the first non-local node
    if (parent_ && parent_ != scene_)
    {
        Node* current = parent_;
        while (!current->IsReplicated())
            current = current->parent_;
        if (current && current != scene_)
            impl_->dependencyNodes_.push_back(current);
    }

    // Let the components add their dependencies
    for (auto i = components_.begin(); i != components_.end(); ++i)
    {
        Component* component = i->Get();
        if (component->IsReplicated())
            component->GetDependencyNodes(impl_->dependencyNodes_);
    }

    // Then check for node attribute changes
    if (!networkState_)
        AllocateNetworkState();

    const ea::vector<AttributeInfo>* attributes = networkState_->attributes_;
    unsigned numAttributes = attributes->size();

    // Check for attribute changes
    for (unsigned i = 0; i < numAttributes; ++i)
    {
        const AttributeInfo& attr = attributes->at(i);

        if (animationEnabled_ && IsAnimatedNetworkAttribute(attr))
            continue;

        OnGetAttribute(attr, networkState_->currentValues_[i]);

        if (networkState_->currentValues_[i] != networkState_->previousValues_[i])
        {
            networkState_->previousValues_[i] = networkState_->currentValues_[i];

            // Mark the attribute dirty in all replication states that are tracking this node
            for (auto j = networkState_->replicationStates_.begin();
                 j != networkState_->replicationStates_.end(); ++j)
            {
                auto* nodeState = static_cast<NodeReplicationState*>(*j);
                nodeState->dirtyAttributes_.Set(i);

                // Add node to the dirty set if not added yet
                if (!nodeState->markedDirty_)
                {
                    nodeState->markedDirty_ = true;
                    nodeState->sceneState_->dirtyNodes_.insert(id_);
                }
            }
        }
    }

    // Finally check for user var changes
    for (auto i = vars_.begin(); i != vars_.end(); ++i)
    {
        auto j = networkState_->previousVars_.find(i->first);
        if (j == networkState_->previousVars_.end() || j->second != i->second)
        {
            networkState_->previousVars_[i->first] = i->second;

            // Mark the var dirty in all replication states that are tracking this node
            for (auto j = networkState_->replicationStates_.begin();
                 j != networkState_->replicationStates_.end(); ++j)
            {
                auto* nodeState = static_cast<NodeReplicationState*>(*j);
                nodeState->dirtyVars_.insert(i->first);

                if (!nodeState->markedDirty_)
                {
                    nodeState->markedDirty_ = true;
                    nodeState->sceneState_->dirtyNodes_.insert(id_);
                }
            }
        }
    }

    networkUpdate_ = false;
}

void Node::CleanupConnection(Connection* connection)
{
    if (impl_->owner_ == connection)
        impl_->owner_ = nullptr;

    if (networkState_)
    {
        for (unsigned i = networkState_->replicationStates_.size() - 1; i < networkState_->replicationStates_.size(); --i)
        {
            if (networkState_->replicationStates_[i]->connection_ == connection)
                networkState_->replicationStates_.erase_at(i);
        }
    }
}

void Node::MarkReplicationDirty()
{
    if (networkState_)
    {
        for (auto j = networkState_->replicationStates_.begin();
             j != networkState_->replicationStates_.end(); ++j)
        {
            auto* nodeState = static_cast<NodeReplicationState*>(*j);
            if (!nodeState->markedDirty_)
            {
                nodeState->markedDirty_ = true;
                nodeState->sceneState_->dirtyNodes_.insert(id_);
            }
        }
    }
}

Node* Node::CreateChild(unsigned id, CreateMode mode, bool temporary)
{
    SharedPtr<Node> newNode(context_->CreateObject<Node>());
    newNode->SetTemporary(temporary);

    // If zero ID specified, or the ID is already taken, let the scene assign
    if (scene_)
    {
        if (!id || scene_->GetNode(id))
            id = scene_->GetFreeNodeID(mode);
        newNode->SetID(id);
    }
    else
        newNode->SetID(id);

    AddChild(newNode.Get());
    return newNode;
}

void Node::AddComponent(Component* component, unsigned id, CreateMode mode)
{
    if (!component)
        return;

    components_.push_back(SharedPtr<Component>(component));

    if (component->GetNode())
        URHO3D_LOGWARNING("Component " + component->GetTypeName() + " already belongs to a node!");

    component->SetNode(this);

    // If zero ID specified, or the ID is already taken, let the scene assign
    if (scene_)
    {
        if (!id || scene_->GetComponent(id))
            id = scene_->GetFreeComponentID(mode);
        component->SetID(id);
        scene_->ComponentAdded(component);
    }
    else
        component->SetID(id);

    component->OnMarkedDirty(this);

    // Check attributes of the new component on next network update, and mark node dirty in all replication states
    component->MarkNetworkUpdate();
    MarkNetworkUpdate();
    MarkReplicationDirty();

    // Send change event
    if (scene_)
    {
        using namespace ComponentAdded;

        VariantMap& eventData = GetEventDataMap();
        eventData[P_SCENE] = scene_;
        eventData[P_NODE] = this;
        eventData[P_COMPONENT] = component;

        scene_->SendEvent(E_COMPONENTADDED, eventData);
    }
}

unsigned Node::GetNumPersistentChildren() const
{
    unsigned ret = 0;

    for (auto i = children_.begin(); i != children_.end(); ++i)
    {
        if (!(*i)->IsTemporary())
            ++ret;
    }

    return ret;
}

unsigned Node::GetNumPersistentComponents() const
{
    unsigned ret = 0;

    for (auto i = components_.begin(); i != components_.end(); ++i)
    {
        if (!(*i)->IsTemporary())
            ++ret;
    }

    return ret;
}

void Node::SetTransformSilent(const Vector3& position, const Quaternion& rotation, const Vector3& scale)
{
    position_ = position;
    rotation_ = rotation;
    scale_ = scale;
}

void Node::SetTransformSilent(const Matrix3x4& matrix)
{
    SetTransformSilent(matrix.Translation(), matrix.Rotation(), matrix.Scale());
}

void Node::OnAttributeAnimationAdded()
{
    if (attributeAnimationInfos_.size() == 1)
        SubscribeToEvent(GetScene(), E_ATTRIBUTEANIMATIONUPDATE, URHO3D_HANDLER(Node, HandleAttributeAnimationUpdate));
}

void Node::OnAttributeAnimationRemoved()
{
    if (attributeAnimationInfos_.empty())
        UnsubscribeFromEvent(GetScene(), E_ATTRIBUTEANIMATIONUPDATE);
}

Animatable* Node::FindAttributeAnimationTarget(const ea::string& name, ea::string& outName)
{
    ea::vector<ea::string> names = name.split('/');
    // Only attribute name
    if (names.size() == 1)
    {
        outName = name;
        return this;
    }
    else
    {
        // Name must in following format: "#0/#1/@component#0/attribute"
        Node* node = this;
        unsigned i = 0;
        for (; i < names.size() - 1; ++i)
        {
            if (names[i].front() != '#')
                break;

            ea::string name = names[i].substr(1, names[i].length() - 1);
            char s = name.front();
            if (s >= '0' && s <= '9')
            {
                unsigned index = ToUInt(name);
                node = node->GetChild(index);
            }
            else
            {
                node = node->GetChild(name, true);
            }

            if (!node)
            {
                URHO3D_LOGERROR("Could not find node by name " + name);
                return nullptr;
            }
        }

        if (i == names.size() - 1)
        {
            outName = names.back();
            return node;
        }

        if (i != names.size() - 2 || names[i].front() != '@')
        {
            URHO3D_LOGERROR("Invalid name " + name);
            return nullptr;
        }

        ea::string componentName = names[i].substr(1, names[i].length() - 1);
        ea::vector<ea::string> componentNames = componentName.split('#');
        if (componentNames.size() == 1)
        {
            Component* component = node->GetComponent(StringHash(componentNames.front()));
            if (!component)
            {
                URHO3D_LOGERROR("Could not find component by name " + name);
                return nullptr;
            }

            outName = names.back();
            return component;
        }
        else
        {
            unsigned index = ToUInt(componentNames[1]);
            ea::vector<Component*> components;
            node->GetComponents(components, StringHash(componentNames.front()));
            if (index >= components.size())
            {
                URHO3D_LOGERROR("Could not find component by name " + name);
                return nullptr;
            }

            outName = names.back();
            return components[index];
        }
    }
}

void Node::SetEnabled(bool enable, bool recursive, bool storeSelf)
{
    // The enabled state of the whole scene can not be changed. SetUpdateEnabled() is used instead to start/stop updates.
    if (GetType() == Scene::GetTypeStatic())
    {
        URHO3D_LOGERROR("Can not change enabled state of the Scene");
        return;
    }

    if (storeSelf)
        enabledPrev_ = enable;

    if (enable != enabled_)
    {
        enabled_ = enable;
        MarkNetworkUpdate();

        // Notify listener components of the state change
        for (auto i = listeners_.begin(); i != listeners_.end();)
        {
            if (*i)
            {
                (*i)->OnNodeSetEnabled(this);
                ++i;
            }
            // If listener has expired, erase from list
            else
                i = listeners_.erase(i);
        }

        // Send change event
        if (scene_)
        {
            using namespace NodeEnabledChanged;

            VariantMap& eventData = GetEventDataMap();
            eventData[P_SCENE] = scene_;
            eventData[P_NODE] = this;

            scene_->SendEvent(E_NODEENABLEDCHANGED, eventData);
        }

        for (auto i = components_.begin(); i != components_.end(); ++i)
        {
            (*i)->OnSetEnabled();

            // Send change event for the component
            if (scene_)
            {
                using namespace ComponentEnabledChanged;

                VariantMap& eventData = GetEventDataMap();
                eventData[P_SCENE] = scene_;
                eventData[P_NODE] = this;
                eventData[P_COMPONENT] = i->Get();

                scene_->SendEvent(E_COMPONENTENABLEDCHANGED, eventData);
            }
        }
    }

    if (recursive)
    {
        for (auto i = children_.begin(); i != children_.end(); ++i)
            (*i)->SetEnabled(enable, recursive, storeSelf);
    }
}

Component* Node::SafeCreateComponent(const ea::string& typeName, StringHash type, CreateMode mode, unsigned id)
{
    // Do not attempt to create replicated components to local nodes, as that may lead to component ID overwrite
    // as replicated components are synced over
    if (mode == REPLICATED && !IsReplicated())
        mode = LOCAL;

    // First check if factory for type exists
    if (!context_->GetTypeName(type).empty())
        return CreateComponent(type, mode, id);
    else
    {
        URHO3D_LOGWARNING("Component type " + type.ToString() + " not known, creating UnknownComponent as placeholder");
        // Else create as UnknownComponent
        SharedPtr<UnknownComponent> newComponent(context_->CreateObject<UnknownComponent>());
        if (typeName.empty() || typeName.starts_with("Unknown", false))
            newComponent->SetType(type);
        else
            newComponent->SetTypeName(typeName);

        AddComponent(newComponent, id, mode);
        return newComponent;
    }
}

void Node::UpdateWorldTransform() const
{
    Matrix3x4 transform = GetTransform();

    // Assume the root node (scene) has identity transform
    if (parent_ == scene_ || !parent_)
    {
        worldTransform_ = transform;
        worldRotation_ = rotation_;
    }
    else
    {
        worldTransform_ = parent_->GetWorldTransform() * transform;
        worldRotation_ = parent_->GetWorldRotation() * rotation_;
    }

    dirty_ = false;
}

void Node::RemoveChild(ea::vector<SharedPtr<Node> >::iterator i)
{
    // Keep a shared pointer to the child about to be removed, to make sure the erase from container completes first. Otherwise
    // it would be possible that other child nodes get removed as part of the node's components' cleanup, causing a re-entrant
    // erase and a crash
    SharedPtr<Node> child(*i);

    // Send change event. Do not send when this node is already being destroyed
    if (Refs() > 0 && scene_)
    {
        using namespace NodeRemoved;

        VariantMap& eventData = GetEventDataMap();
        eventData[P_SCENE] = scene_;
        eventData[P_PARENT] = this;
        eventData[P_NODE] = child;

        scene_->SendEvent(E_NODEREMOVED, eventData);
    }

    child->parent_ = nullptr;
    child->MarkDirty();
    child->MarkNetworkUpdate();
    if (scene_)
        scene_->NodeRemoved(child.Get());

    children_.erase(i);
}

void Node::GetChildrenRecursive(ea::vector<Node*>& dest) const
{
    for (auto i = children_.begin(); i != children_.end(); ++i)
    {
        Node* node = i->Get();
        dest.push_back(node);
        if (!node->children_.empty())
            node->GetChildrenRecursive(dest);
    }
}

void Node::GetChildrenWithComponentRecursive(ea::vector<Node*>& dest, StringHash type) const
{
    for (auto i = children_.begin(); i != children_.end(); ++i)
    {
        Node* node = i->Get();
        if (node->HasComponent(type))
            dest.push_back(node);
        if (!node->children_.empty())
            node->GetChildrenWithComponentRecursive(dest, type);
    }
}

void Node::GetComponentsRecursive(ea::vector<Component*>& dest, StringHash type) const
{
    for (auto i = components_.begin(); i != components_.end(); ++i)
    {
        if ((*i)->GetType() == type)
            dest.push_back(i->Get());
    }
    for (auto i = children_.begin(); i != children_.end(); ++i)
        (*i)->GetComponentsRecursive(dest, type);
}

void Node::GetChildrenWithTagRecursive(ea::vector<Node*>& dest, const ea::string& tag) const
{
    for (auto i = children_.begin(); i != children_.end(); ++i)
    {
        Node* node = i->Get();
        if (node->HasTag(tag))
            dest.push_back(node);
        if (!node->children_.empty())
            node->GetChildrenWithTagRecursive(dest, tag);
    }
}

Node* Node::CloneRecursive(Node* parent, SceneResolver& resolver, CreateMode mode)
{
    // Create clone node
    Node* cloneNode = parent->CreateChild(0, (mode == REPLICATED && IsReplicated()) ? REPLICATED : LOCAL);
    resolver.AddNode(id_, cloneNode);

    // Copy attributes
    const ea::vector<AttributeInfo>* attributes = GetAttributes();
    for (unsigned j = 0; j < attributes->size(); ++j)
    {
        const AttributeInfo& attr = attributes->at(j);
        // Do not copy network-only attributes, as they may have unintended side effects
        if (attr.mode_ & AM_FILE)
        {
            Variant value;
            OnGetAttribute(attr, value);
            cloneNode->OnSetAttribute(attr, value);
        }
    }

    // Clone components
    for (auto i = components_.begin(); i != components_.end(); ++i)
    {
        Component* component = i->Get();
        if (component->IsTemporary())
            continue;

        Component* cloneComponent = cloneNode->CloneComponent(component,
            (mode == REPLICATED && component->IsReplicated()) ? REPLICATED : LOCAL, 0);
        if (cloneComponent)
            resolver.AddComponent(component->GetID(), cloneComponent);
    }

    // Clone child nodes recursively
    for (auto i = children_.begin(); i != children_.end(); ++i)
    {
        Node* node = i->Get();
        if (node->IsTemporary())
            continue;

        node->CloneRecursive(cloneNode, resolver, mode);
    }

    if (scene_)
    {
        using namespace NodeCloned;

        VariantMap& eventData = GetEventDataMap();
        eventData[P_SCENE] = scene_;
        eventData[P_NODE] = this;
        eventData[P_CLONENODE] = cloneNode;

        scene_->SendEvent(E_NODECLONED, eventData);
    }

    return cloneNode;
}

void Node::RemoveComponent(ea::vector<SharedPtr<Component> >::iterator i)
{
    // Send node change event. Do not send when already being destroyed
    if (Refs() > 0 && scene_)
    {
        using namespace ComponentRemoved;

        VariantMap& eventData = GetEventDataMap();
        eventData[P_SCENE] = scene_;
        eventData[P_NODE] = this;
        eventData[P_COMPONENT] = (*i);

        scene_->SendEvent(E_COMPONENTREMOVED, eventData);
    }

    RemoveListener(i->Get());
    if (scene_)
        scene_->ComponentRemoved(i->Get());
    (*i)->SetNode(nullptr);
    components_.erase(i);
}

void Node::HandleAttributeAnimationUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace AttributeAnimationUpdate;

    UpdateAttributeAnimations(eventData[P_TIMESTEP].GetFloat());
}

}
