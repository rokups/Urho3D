//
// Copyright (c) 2008-2019 the Urho3D project.
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

#include "../Container/FlagSet.h"
#include <EASTL/shared_ptr.h>
#include "../Core/Variant.h"

namespace Urho3D
{

enum AttributeMode
{
    /// Attribute shown only in the editor, but not serialized.
    AM_EDIT = 0x0,
    /// Attribute used for file serialization.
    AM_FILE = 0x1,
    /// Attribute used for network replication.
    AM_NET = 0x2,
    /// Attribute used for both file serialization and network replication (default).
    AM_DEFAULT = 0x3,
    /// Attribute should use latest data grouping instead of delta update in network replication.
    AM_LATESTDATA = 0x4,
    /// Attribute should not be shown in the editor.
    AM_NOEDIT = 0x8,
    /// Attribute is a node ID and may need rewriting.
    AM_NODEID = 0x10,
    /// Attribute is a component ID and may need rewriting.
    AM_COMPONENTID = 0x20,
    /// Attribute is a node ID vector where first element is the amount of nodes.
    AM_NODEIDVECTOR = 0x40,
    /// Attribute is readonly. Can't be used with binary serialized objects.
    AM_FILEREADONLY = 0x81,
};
URHO3D_FLAGSET(AttributeMode, AttributeModeFlags);

class Serializable;

/// Abstract base class for invoking attribute accessors.
class URHO3D_API AttributeAccessor : public RefCounted
{
public:
    /// Construct.
    AttributeAccessor() = default;
    /// Get the attribute.
    virtual void Get(const Serializable* ptr, Variant& dest) const = 0;
    /// Set the attribute.
    virtual void Set(Serializable* ptr, const Variant& src) = 0;
};

/// Description of an automatically serializable variable.
struct AttributeInfo
{
    /// Construct empty.
    AttributeInfo() = default;
#ifndef SWIG
    /// Construct attribute.
    AttributeInfo(VariantType type, const char* name, const stl::shared_ptr<AttributeAccessor>& accessor, const char** enumNames, const Variant& defaultValue, AttributeModeFlags mode) :
        type_(type),
        name_(name),
        enumNames_(enumNames),
        accessor_(accessor),
        defaultValue_(defaultValue),
        mode_(mode)
    {
    }
#endif
    /// Construct attribute.
    AttributeInfo(VariantType type, const char* name, const stl::shared_ptr<AttributeAccessor>& accessor, const Vector<String>& enumNames, const Variant& defaultValue, AttributeModeFlags mode) :
        type_(type),
        name_(name),
        enumNames_(nullptr),
        enumNamesStorage_(enumNames),
        accessor_(accessor),
        defaultValue_(defaultValue),
        mode_(mode)
    {
        InitializeEnumNamesFromStorage();
    }

    /// Copy attribute info.
    AttributeInfo(const AttributeInfo& other)
    {
        type_ = other.type_;
        name_ = other.name_;
        enumNames_ = other.enumNames_;
        accessor_ = other.accessor_;
        defaultValue_ = other.defaultValue_;
        mode_ = other.mode_;
        metadata_ = other.metadata_;
        ptr_ = other.ptr_;
        enumNamesStorage_ = other.enumNamesStorage_;

        if (!enumNamesStorage_.Empty())
            InitializeEnumNamesFromStorage();
    }

    /// Get attribute metadata.
    const Variant& GetMetadata(const StringHash& key) const
    {
        auto elem = metadata_.Find(key);
        return elem != metadata_.End() ? elem->second_ : Variant::EMPTY;
    }

    /// Get attribute metadata of specified type.
    template <class T> T GetMetadata(const StringHash& key) const
    {
        return GetMetadata(key).Get<T>();
    }

    /// Instance equality operator.
    bool operator ==(const AttributeInfo& rhs) const
    {
        return this == &rhs;
    }

    /// Instance inequality operator.
    bool operator !=(const AttributeInfo& rhs) const
    {
        return this != &rhs;
    }

    /// Attribute type.
    VariantType type_ = VAR_NONE;
    /// Name.
    String name_;
    /// Enum names.
    const char** enumNames_ = nullptr;
    /// Helper object for accessor mode.
    stl::shared_ptr<AttributeAccessor> accessor_;
    /// Default value for network replication.
    Variant defaultValue_;
    /// Attribute mode: whether to use for serialization, network replication, or both.
    AttributeModeFlags mode_ = AM_DEFAULT;
    /// Attribute metadata.
    VariantMap metadata_;
    /// Attribute data pointer if elsewhere than in the Serializable.
    void* ptr_ = nullptr;
    /// List of enum names. Used when names can not be stored externally.
    Vector<String> enumNamesStorage_;
    /// List of enum name pointers. Front of this vector will be assigned to enumNames_ when enumNamesStorage_ is in use.
    Vector<const char*> enumNamesPointers_;

private:
    void InitializeEnumNamesFromStorage()
    {
        if (enumNamesStorage_.Empty())
            enumNames_ = nullptr;
        else
        {
            for (const auto& enumName : enumNamesStorage_)
                enumNamesPointers_.EmplaceBack(enumName.CString());
            enumNamesPointers_.EmplaceBack(nullptr);
            enumNames_ = &enumNamesPointers_.Front();
        }
    }
};

/// Attribute handle returned by Context::RegisterAttribute and used to chain attribute setup calls.
struct AttributeHandle
{
    friend class Context;
public:
    /// Construct default.
    AttributeHandle() = default;
    /// Construct from another handle.
    AttributeHandle(const AttributeHandle& another) = default;
private:
    /// Attribute info.
    AttributeInfo* attributeInfo_ = nullptr;
    /// Network attribute info.
    AttributeInfo* networkAttributeInfo_ = nullptr;
public:
    /// Set metadata.
    AttributeHandle& SetMetadata(StringHash key, const Variant& value)
    {
        if (attributeInfo_)
            attributeInfo_->metadata_[key] = value;
        if (networkAttributeInfo_)
            networkAttributeInfo_->metadata_[key] = value;
        return *this;
    }
};

}
