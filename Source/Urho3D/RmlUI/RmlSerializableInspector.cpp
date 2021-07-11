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
#include "../Precompiled.h"

#include "../Core/Context.h"
#include "../IO/Log.h"
#include "../RmlUI/RmlSerializableInspector.h"
#include "../RmlUI/RmlUI.h"

#include <RmlUi/Core/Context.h>

namespace Urho3D
{

/// Controls type of atttribute widget. Use plain enum for interfacing with RmlUI.
enum AttributeType
{
    AttributeType_Undefined,
    AttributeType_Bool      = 1,
    AttributeType_Number    = 2,
    AttributeType_Enum      = 3,
};

struct RmlSerializableAttribute
{
    WeakPtr<Serializable> serializable_;
    unsigned index_{};
    VariantType internalType_{};
    Rml::String name_;
    Rml::String enumSelector_;
    Rml::StringList enumNames_;
    int type_{}; // AttributeType

    Rml::String GetEnumStringValue() const
    {
        const int index = serializable_->GetAttribute(index_).GetInt();
        return index < enumNames_.size() ? enumNames_[index] : "";
    }

    void SetEnumStringValue(const Rml::String& value)
    {
        const auto iter = enumNames_.find(value);
        if (iter != enumNames_.end())
            serializable_->SetAttribute(index_, static_cast<int>(iter - enumNames_.begin()));
    }

    Rml::Variant GetValue()
    {
        if (!serializable_)
            return Rml::Variant{};

        switch (type_)
        {
        case AttributeType_Bool:
            return Rml::Variant{ serializable_->GetAttribute(index_).GetBool() };

        case AttributeType_Enum:
            return Rml::Variant{ GetEnumStringValue() };

        case AttributeType_Number:
            switch (internalType_)
            {
            case VAR_INT:
                return Rml::Variant{ serializable_->GetAttribute(index_).GetInt() };

            case VAR_INT64:
                return Rml::Variant{ static_cast<int64_t>(serializable_->GetAttribute(index_).GetInt64()) };

            case VAR_FLOAT:
                return Rml::Variant{ serializable_->GetAttribute(index_).GetFloat() };

            case VAR_DOUBLE:
                return Rml::Variant{ serializable_->GetAttribute(index_).GetDouble() };

            default:
                return Rml::Variant{};
            }

        default:
            return Rml::Variant{};
        }
    }

    void SetValue(const Rml::Variant& variant)
    {
        if (!serializable_)
            return;

        switch (type_)
        {
        case AttributeType_Bool:
            serializable_->SetAttribute(index_, variant.Get<bool>());
            break;

        case AttributeType_Enum:
            SetEnumStringValue(variant.Get<Rml::String>());
            break;

        case AttributeType_Number:
            switch (internalType_)
            {
            case VAR_INT:
                serializable_->SetAttribute(index_, variant.Get<int>());
                break;

            case VAR_INT64:
                serializable_->SetAttribute(index_, static_cast<long long>(variant.Get<int64_t>()));
                break;

            case VAR_FLOAT:
                serializable_->SetAttribute(index_, variant.Get<float>());
                break;

            case VAR_DOUBLE:
                serializable_->SetAttribute(index_, variant.Get<double>());
                break;

            default:
                break;
            }

        default:
            break;
        }
    }
};

RmlSerializableInspector::RmlSerializableInspector(Context* context)
    : RmlUIComponent(context)
{
}

RmlSerializableInspector::~RmlSerializableInspector()
{
}

void RmlSerializableInspector::RegisterObject(Context* context)
{
    context->RegisterFactory<RmlSerializableInspector>();
}

void RmlSerializableInspector::Connect(Serializable* serializable)
{
    if (!serializable || !model_)
    {
        URHO3D_LOGERROR("Cannot connect RmlSerializableInspector to object before initialization");
        return;
    }

    serializable_ = serializable;
    type_ = serializable->GetTypeName();
    attributes_.clear();

    const auto attributes = serializable->GetAttributes();
    if (!attributes)
        return;

    unsigned index = 0;
    for (const AttributeInfo& attributeInfo : *attributes)
    {
        RmlSerializableAttribute attribute;
        attribute.index_ = index;
        attribute.internalType_ = attributeInfo.type_;
        attribute.serializable_ = serializable_;
        attribute.name_ = attributeInfo.name_;

        if (attributeInfo.type_ == VAR_BOOL)
            attribute.type_ = AttributeType_Bool;
        else if (attributeInfo.enumNames_ != nullptr)
        {
            attribute.type_ = AttributeType_Enum;
            for (unsigned i = 0; attributeInfo.enumNames_[i] != nullptr; ++i)
                attribute.enumNames_.push_back(attributeInfo.enumNames_[i]);

            attribute.enumSelector_ = "<select data-value='attribute.value' style='width: 90%'>";
            for (const auto& option : attribute.enumNames_)
                attribute.enumSelector_ += Format("<option value='{}'>{}</option>", option, option);
            attribute.enumSelector_ += "</select>";
        }
        else if (attributeInfo.type_ == VAR_INT || attributeInfo.type_ == VAR_INT64
            || attributeInfo.type_ == VAR_FLOAT || attributeInfo.type_ == VAR_DOUBLE)
        {
            attribute.type_ = AttributeType_Number;
        }

        if (attribute.type_ != AttributeType_Undefined)
            attributes_.push_back(attribute);

        ++index;
    }

    model_.DirtyVariable("attributes");
    model_.DirtyVariable("type");
}

void RmlSerializableInspector::OnNodeSet(Node* node)
{
    BaseClassName::OnNodeSet(node);
    RmlUI* rmlUI = GetUI();
    Rml::Context* rmlContext = rmlUI->GetRmlContext();

    if (node != nullptr && !model_)
    {
        Rml::DataModelConstructor constructor = rmlContext->CreateDataModel("RmlSerializableInspector_model");
        if (!constructor)
            return;

        constructor.RegisterArray<Rml::StringList>();
        static const auto getVariant = [](const Rml::Variant& src, Rml::Variant& dest) { dest = src; };
        static const auto setVariant = [](Rml::Variant& dest, const Rml::Variant& src) { dest = src; };
        constructor.RegisterScalar<Rml::Variant>(getVariant, setVariant);
        if (auto attributeHandle = constructor.RegisterStruct<RmlSerializableAttribute>())
        {
            attributeHandle.RegisterMember("name", &RmlSerializableAttribute::name_);
            attributeHandle.RegisterMember("type", &RmlSerializableAttribute::type_);
            attributeHandle.RegisterMember("enum_selector", &RmlSerializableAttribute::enumSelector_);
            attributeHandle.RegisterMember("value", &RmlSerializableAttribute::GetValue, &RmlSerializableAttribute::SetValue);
        }
        constructor.RegisterArray<ea::vector<RmlSerializableAttribute>>();

        constructor.Bind("attributes", &attributes_);
        constructor.Bind("type", &type_);

        model_ = constructor.GetModelHandle();

        SetResource("UI/SerializableInspector.rml");
        SetOpen(true);

        SubscribeToEvent(rmlUI, "RmlSerializableInspector_CloseWindow",
            [this](StringHash, VariantMap& args)
        {
            Remove();
        });
    }
    else if (node == nullptr && model_)
    {
        rmlContext->RemoveDataModel("RmlSerializableInspector_model");
        model_ = nullptr;
    }
}

void RmlSerializableInspector::Update(float timeStep)
{
    if (!serializable_ || !model_)
    {
        Remove();
        return;
    }

    model_.DirtyVariable("attributes");
}

}
