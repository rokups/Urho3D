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

#include <limits>

#include <Urho3D/SystemUI/SystemUI.h>
#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/Scene/Serializable.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Technique.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/Graphics/RenderPath.h>
#include "AttributeInspector.h"
#include "Widgets.h"

#include <IconFontCppHeaders/IconsFontAwesome5.h>
#include <ImGui/imgui_internal.h>
#include <ImGui/imgui_stdlib.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Graphics/Graphics.h>

namespace Urho3D
{

VariantType supportedVariantTypes[] = {
    VAR_INT,
    VAR_BOOL,
    VAR_FLOAT,
    VAR_VECTOR2,
    VAR_VECTOR3,
    VAR_VECTOR4,
    VAR_QUATERNION,
    VAR_COLOR,
    VAR_STRING,
    //VAR_BUFFER,
    //VAR_RESOURCEREF,
    //VAR_RESOURCEREFLIST,
    //VAR_VARIANTVECTOR,
    //VAR_VARIANTMAP,
    VAR_INTRECT,
    VAR_INTVECTOR2,
    VAR_MATRIX3,
    VAR_MATRIX3X4,
    VAR_MATRIX4,
    VAR_DOUBLE,
    //VAR_STRINGVECTOR,
    VAR_RECT,
    VAR_INTVECTOR3,
    VAR_INT64,
};
const int MAX_SUPPORTED_VAR_TYPES = SDL_arraysize(supportedVariantTypes);

const char* supportedVariantNames[] = {
    "Int",
    "Bool",
    "Float",
    "Vector2",
    "Vector3",
    "Vector4",
    "Quaternion",
    "Color",
    "String",
    //"Buffer",
    //"ResourceRef",
    //"ResourceRefList",
    //"VariantVector",
    //"VariantMap",
    "IntRect",
    "IntVector2",
    "Matrix3",
    "Matrix3x4",
    "Matrix4",
    "Double",
    //"StringVector",
    "Rect",
    "IntVector3",
    "Int64",
};

bool RenderResourceRef(ResourceRef& ref, Object* eventSender)
{
    SharedPtr<Resource> resource;
    auto modified = false;
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = ui::GetStyle();
    float w = ui::CalcItemWidth();

    // Reduce resource input width to make space for two buttons.
    ui::SetNextItemWidth(w - (style.ItemSpacing.x + ui::IconButtonSize()) * 2);
    ui::InputTextWithHint("", "Drag & Drop a resource", &ref.name_, ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly);
    // Resources are assigned by dropping on to them.
    if (ui::BeginDragDropTarget())
    {
        const Variant& payload = ui::AcceptDragDropVariant(ref.type_.ToString());
        if (!payload.IsEmpty())
        {
            ref.name_ = payload.GetString();
            modified = true;
        }
        ui::EndDragDropTarget();
    }

    // Locate resource button.
    ui::SameLine();
    if (ui::IconButton(ICON_FA_CROSSHAIRS))
        eventSender->SendEvent(E_INSPECTORLOCATERESOURCE, InspectorLocateResource::P_NAME, ref.name_);
    ui::SetHelpTooltip("Locate resource.");

    // Clear resource button.
    ui::SameLine();
    if (ui::IconButton(ICON_FA_TRASH))
    {
        ref.name_.clear();
        modified = true;
    }
    ui::SetHelpTooltip("Clear resource.");
    return modified;
}

bool RenderAttribute(ea::string_view title, Variant& value, const Color& color, ea::string_view tooltip,
    const AttributeInfo* info, Object* eventSender, float item_width)
{
    const float floatMin = -std::numeric_limits<float>::infinity();
    const float floatMax = std::numeric_limits<float>::infinity();
    const double doubleMin = -std::numeric_limits<double>::infinity();
    const double doubleMax = std::numeric_limits<double>::infinity();
    int intMin = std::numeric_limits<int>::min();
    int intMax = std::numeric_limits<int>::max();
    const float floatStep = 0.1f;
    const ImGuiStyle& style = ui::GetStyle();

    // FIXME: string_view is used as zero-terminated C string. Fix this when https://github.com/ocornut/imgui/pull/3038 is merged. Until then do not use string slices with attribute inspector!
    if (title.empty())
    {
        if (info != nullptr)
            title = info->name_;
        else
            title = "";
    }

    bool modified = false;
    bool openAttributeMenu = false;
    bool showHelperLabels = false;

    // Render label
    ui::ItemLabelFlags flags = ui::ItemLabelFlag::Left;
    ItemLabel(title, &color, flags);
    openAttributeMenu |= ui::IsItemClicked(MOUSEB_RIGHT);
    if (item_width != 0)
        ui::PushItemWidth(item_width);

    if (info != nullptr && info->enumNames_ != nullptr)
    {
        // Count enum names.
        auto comboValuesNum = 0;
        for (; info->enumNames_ && info->enumNames_[++comboValuesNum];) { }
        // Render enums.
        assert(info != nullptr);
        int current = 0;
        if (info->type_ == VAR_INT)
            current = value.GetInt();
        else if (info->type_ == VAR_STRING)
            current = static_cast<int>(GetStringListIndex(value.GetString().c_str(), info->enumNames_, 0));
        else
            assert(false);

        modified |= ui::Combo("###enum", &current, info->enumNames_, comboValuesNum);
        if (modified)
        {
            if (info->type_ == VAR_INT)
                value = current;
            else if (info->type_ == VAR_STRING)
                value = info->enumNames_[current];
        }
    }
    else
    {
        switch (info ? info->type_ : value.GetType())
        {
        case VAR_NONE:
            ui::TextUnformatted("None");
            break;
        case VAR_INT:
        {
            if (title.ends_with(" Mask") || title.ends_with(" Bits"))
            {
                unsigned v = value.GetUInt();
                modified |= ui::MaskSelector("", &v);
                if (modified)
                    value = v;
            }
            else
            {
                int v = value.GetInt();
                modified |= ui::DragInt("", &v, 1, M_MIN_INT, M_MAX_INT);
                if (modified)
                    value = v;
            }
            break;
        }
        case VAR_BOOL:
        {
            auto v = value.GetBool();
            if (flags & ui::ItemLabelFlag::Right)
                ui::ItemAlign(ui::GetFrameHeight());    // Align checkbox to the right side.
            modified |= ui::Checkbox("", &v);
            if (modified)
                value = v;
            break;
        }
        case VAR_FLOAT:
        {
            auto v = value.GetFloat();
            modified |= ui::DragFloat("", &v, floatStep, floatMin, floatMax, "%.3f");
            if (modified)
                value = v;
            break;
        }
        case VAR_VECTOR2:
        {
            auto& v = value.GetVector2();
            const char* formats[] = {"X=%.3f", "Y=%.3f"};
            if (showHelperLabels)
                modified |= ui::DragScalarFormatsN("", ImGuiDataType_Float, const_cast<float*>(&v.x_), 2, floatStep, &floatMin, &floatMax, formats);
            else
                modified |= ui::DragScalarN("", ImGuiDataType_Float, const_cast<float*>(&v.x_), 2, floatStep, &floatMin, &floatMax, "%.3f");
            break;
        }
        case VAR_VECTOR3:
        {
            auto& v = value.GetVector3();
            const char* formats[] = {"X=%.3f", "Y=%.3f", "Z=%.3f"};
            if (showHelperLabels)
                modified |= ui::DragScalarFormatsN("", ImGuiDataType_Float, const_cast<float*>(&v.x_), 3, floatStep, &floatMin, &floatMax, formats);
            else
                modified |= ui::DragScalarN("", ImGuiDataType_Float, const_cast<float*>(&v.x_), 3, floatStep, &floatMin, &floatMax, "%.3f");
            break;
        }
        case VAR_VECTOR4:
        {
            auto& v = value.GetVector4();
            const char* formats[] = {"X=%.3f", "Y=%.3f", "Z=%.3f", "W=%.3f"};
            if (showHelperLabels)
                modified |= ui::DragScalarFormatsN("", ImGuiDataType_Float, const_cast<float*>(&v.x_), 4, floatStep, &floatMin, &floatMax, formats);
            else
                modified |= ui::DragScalarN("", ImGuiDataType_Float, const_cast<float*>(&v.x_), 4, floatStep, &floatMin, &floatMax, "%.3f");
            break;
        }
        case VAR_QUATERNION:
        {
            Vector3 currentAngles = value.GetQuaternion().EulerAngles();
            Vector3& angles = *ui::GetUIState<Vector3>(currentAngles);
            Vector3 anglesInitial = angles;
            if (showHelperLabels)
            {
                const char* formats[] = {"P=%.3f", "Y=%.3f", "R=%.3f"};
                modified |= ui::DragScalarFormatsN("", ImGuiDataType_Float, &angles.x_, 3, floatStep, &floatMin, &floatMax, formats);
            }
            else
                modified |= ui::DragScalarN("", ImGuiDataType_Float, &angles.x_, 3, floatStep, &floatMin, &floatMax, "%.3f");

            if (modified)
            {
                if (angles.x_ != anglesInitial.x_)
                    angles.x_ += angles.x_ > 360.0f ? -360.0f : angles.x_ < 0.0f ? +360.0f : 0.0f;
                if (angles.y_ != anglesInitial.y_)
                    angles.y_ += angles.y_ > 360.0f ? -360.0f : angles.y_ < 0.0f ? +360.0f : 0.0f;
                if (angles.z_ != anglesInitial.z_)
                    angles.z_ += angles.z_ > 360.0f ? -360.0f : angles.z_ < 0.0f ? +360.0f : 0.0f;
                value = Quaternion(angles);
            }
            break;
        }
        case VAR_COLOR:
        {
            auto& v = value.GetColor();
            modified |= ui::ColorEdit4("", const_cast<float*>(&v.r_));
            break;
        }
        case VAR_STRING:
        {
            auto& v = const_cast<ea::string&>(value.GetString());
            auto* buffer = ui::GetUIState<ea::string>(v.c_str());
            bool dirty = v.compare(buffer->c_str()) != 0;
            if (dirty)
                ui::PushStyleColor(ImGuiCol_Text, ui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            modified |= ui::InputTextWithHint("", "Enter text and press [Enter]", buffer, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_NoUndoRedo);
            if (dirty)
                ui::PopStyleColor();
            if (modified)
                value = *buffer;
            break;
        }
        // case VAR_BUFFER:
        case VAR_RESOURCEREF:
        {
            // TODO: This may return ResourceRef with empty name but set type. This can happen if object has a runtime-created resource set
            //  programatically. Attribute getter may also return such value (see Light::GetShapeTextureAttr()). These cases may need
            //  additional handling.
            ResourceRef ref = value.GetResourceRef();
            if (ref.type_ == StringHash::ZERO && info)
                ref.type_ = info->defaultValue_.GetResourceRef().type_;

            modified = RenderResourceRef(ref, eventSender);

            if (modified)
                value = ref;
            break;
        }
        case VAR_RESOURCEREFLIST:
        {
            const ResourceRefList& refList = value.GetResourceRefList();
            float posX = ui::GetCursorPosX();
            for (auto i = 0; i < refList.names_.size(); i++)
            {
                UI_ID(i)
                {
                    ResourceRef ref(refList.type_, refList.names_[i]);
                    ui::SetCursorPosX(posX);
                    modified |= RenderResourceRef(ref, eventSender);
                    if (modified)
                    {
                        ResourceRefList newRefList(refList);
                        newRefList.names_[i] = ref.name_;
                        value = newRefList;
                        break;
                    }
                }
            }
            if (refList.names_.empty())
            {
                ui::SetCursorPosY(ui::GetCursorPosY() + 5);
                ui::TextUnformatted("...");
            }
            // TODO: Do we need list expansion?
            break;
        }
        // case VAR_VARIANTVECTOR:
        case VAR_VARIANTMAP:
        {
            struct VariantMapState
            {
                // Map key.
                ea::string key_{};
                // Index of type in supportedVariantTypes array.
                int valueTypeIndex_ = 0;
            };
            ui::IdScope idScope(VAR_VARIANTMAP);
            auto* map = value.GetVariantMapPtr();
            auto* mapState = ui::GetUIState<VariantMapState>();

            // New key insertion
            ui::SetNextItemWidth(ui::CalcItemWidth() * 0.325f);
            ui::Combo("###key-type", &mapState->valueTypeIndex_, supportedVariantNames, MAX_SUPPORTED_VAR_TYPES);
            ui::SameLine();
            ui::SetNextItemWidth(ui::CalcItemWidth() * 0.675f - style.ItemSpacing.x);
            if (ui::InputTextWithHint("##key", "Enter key and press [Enter]", &mapState->key_, ImGuiInputTextFlags_EnterReturnsTrue))
            {
                if (map->find(mapState->key_.c_str()) == map->end())   // TODO: Show warning about duplicate name
                {
                    map->insert({ mapState->key_.c_str(), Variant{ supportedVariantTypes[mapState->valueTypeIndex_] } });
                    mapState->key_.clear();
                    mapState->valueTypeIndex_ = 0;
                    modified = true;
                }
            }

            // Keys and values.
            unsigned index = 0;
            for (auto it = map->begin(); it != map->end(); it++)
            {
                VariantType type = it->second.GetType();
                if (type == VAR_RESOURCEREFLIST || type == VAR_VARIANTMAP || type == VAR_VARIANTVECTOR ||
                    type == VAR_BUFFER || type == VAR_VOIDPTR || type == VAR_PTR)
                    // TODO: Support nested collections.
                    continue;

#if URHO3D_HASH_DEBUG
                const ea::string& name = StringHash::GetGlobalStringHashRegister()->GetString(it->first);
                const char* keyName = (name.empty() ? it->first.ToString() : name).c_str();
#else
                const char* keyName = it->first_.ToString().c_str();
#endif

                ui::IdScope entryIdScope(index++);
                // FIXME: Number of components in rendered attribute affects item width and causes misalignment.
                ui::ItemLabel(keyName);
                modified |= RenderAttribute("", it->second, Color::WHITE, "", nullptr, eventSender, ui::CalcItemWidth() - ui::IconButtonSize());
                // Delete button.
                ui::SameLine();
                if (ui::Button(ICON_FA_TRASH))
                {
                    it = map->erase(it);
                    modified |= true;
                    break;
                }
            }
            ui::Separator();

            break;
        }
        case VAR_INTRECT:
        {
            auto& v = value.GetIntRect();
            const char* formats[] = {"L=%d", "T=%d", "B=%d", "R=%d"};
            if (showHelperLabels)
                modified |= ui::DragScalarFormatsN("", ImGuiDataType_S32, const_cast<int*>(&v.left_), 4, 1, &intMin, &intMax, formats, 1.0f);
            else
                modified |= ui::DragScalarN("", ImGuiDataType_S32, const_cast<int*>(&v.left_), 4, 1, &intMin, &intMax, "%.3f", 1.0f);
            break;
        }
        case VAR_INTVECTOR2:
        {
            auto& v = value.GetIntVector2();
            const char* formats[] = {"X=%d", "Y=%d"};
            if (showHelperLabels)
                modified |= ui::DragScalarFormatsN("", ImGuiDataType_S32, const_cast<int*>(&v.x_), 2, 1, &intMin, &intMax, formats, 1.0f);
            else
                modified |= ui::DragScalarN("", ImGuiDataType_S32, const_cast<int*>(&v.x_), 2, 1, &intMin, &intMax, "%d", 1.0f);
            break;
        }
        case VAR_MATRIX3:
        {
            auto& v = value.GetMatrix3();
            const char* formats[3][3] = {
                {"M00=%.3f", "M01=%.3f", "M02=%.3f"},
                {"M10=%.3f", "M11=%.3f", "M12=%.3f"},
                {"M20=%.3f", "M21=%.3f", "M22=%.3f"}
            };
            ui::BeginGroup();
            if (showHelperLabels)
            {
                modified |= ui::DragScalarFormatsN("###m0", ImGuiDataType_Float, const_cast<float*>(&v.m00_), 3, floatStep, &floatMin, &floatMax, formats[0]);
                modified |= ui::DragScalarFormatsN("###mm1", ImGuiDataType_Float, const_cast<float*>(&v.m10_), 3, floatStep, &floatMin, &floatMax, formats[0]);
                modified |= ui::DragScalarFormatsN("###mm2", ImGuiDataType_Float, const_cast<float*>(&v.m20_), 3, floatStep, &floatMin, &floatMax, formats[0]);
            }
            else
            {
                modified |= ui::DragScalarN("###m0", ImGuiDataType_Float, const_cast<float*>(&v.m00_), 3, floatStep, &floatMin, &floatMax, "%.3f");
                modified |= ui::DragScalarN("###mm1", ImGuiDataType_Float, const_cast<float*>(&v.m10_), 3, floatStep, &floatMin, &floatMax, "%.3f");
                modified |= ui::DragScalarN("###mm2", ImGuiDataType_Float, const_cast<float*>(&v.m20_), 3, floatStep, &floatMin, &floatMax, "%.3f");
            }
            ui::EndGroup();
            break;
        }
        case VAR_MATRIX3X4:
        {
            auto& v = value.GetMatrix3x4();
            const char* formats[3][4] = {
                {"M00=%.3f", "M01=%.3f", "M02=%.3f", "M03=%.3f"},
                {"M10=%.3f", "M11=%.3f", "M12=%.3f", "M13=%.3f"},
                {"M20=%.3f", "M21=%.3f", "M22=%.3f", "M23=%.3f"}
            };
            ui::BeginGroup();
            if (showHelperLabels)
            {
                modified |= ui::DragScalarFormatsN("###mm0", ImGuiDataType_Float, const_cast<float*>(&v.m00_), 4, floatStep, &floatMin, &floatMax, formats[0]);
                modified |= ui::DragScalarFormatsN("###mm1", ImGuiDataType_Float, const_cast<float*>(&v.m10_), 4, floatStep, &floatMin, &floatMax, formats[0]);
                modified |= ui::DragScalarFormatsN("###mm2", ImGuiDataType_Float, const_cast<float*>(&v.m20_), 4, floatStep, &floatMin, &floatMax, formats[0]);
            }
            else
            {
                modified |= ui::DragScalarN("###mm0", ImGuiDataType_Float, const_cast<float*>(&v.m00_), 4, floatStep, &floatMin, &floatMax, "%.3f");
                modified |= ui::DragScalarN("###mm1", ImGuiDataType_Float, const_cast<float*>(&v.m10_), 4, floatStep, &floatMin, &floatMax, "%.3f");
                modified |= ui::DragScalarN("###mm2", ImGuiDataType_Float, const_cast<float*>(&v.m20_), 4, floatStep, &floatMin, &floatMax, "%.3f");
            }
            ui::EndGroup();
            break;
        }
        case VAR_MATRIX4:
        {
            auto& v = value.GetMatrix4();
            const char* formats[4][4] = {
                {"M00=%.3f", "M01=%.3f", "M02=%.3f", "M03=%.3f"},
                {"M10=%.3f", "M11=%.3f", "M12=%.3f", "M13=%.3f"},
                {"M20=%.3f", "M21=%.3f", "M22=%.3f", "M23=%.3f"},
                {"M30=%.3f", "M31=%.3f", "M32=%.3f", "M33=%.3f"},
            };
            ui::BeginGroup();
            if (showHelperLabels)
            {
                modified |= ui::DragScalarFormatsN("###mm0", ImGuiDataType_Float, const_cast<float*>(&v.m00_), 4, floatStep, &floatMin, &floatMax, formats[0]);
                modified |= ui::DragScalarFormatsN("###mm1", ImGuiDataType_Float, const_cast<float*>(&v.m10_), 4, floatStep, &floatMin, &floatMax, formats[0]);
                modified |= ui::DragScalarFormatsN("###mm2", ImGuiDataType_Float, const_cast<float*>(&v.m20_), 4, floatStep, &floatMin, &floatMax, formats[0]);
                modified |= ui::DragScalarFormatsN("###mm3", ImGuiDataType_Float, const_cast<float*>(&v.m30_), 4, floatStep, &floatMin, &floatMax, formats[0]);
            }
            else
            {
                modified |= ui::DragScalarN("###mm0", ImGuiDataType_Float, const_cast<float*>(&v.m00_), 4, floatStep, &floatMin, &floatMax, "%.3f");
                modified |= ui::DragScalarN("###mm1", ImGuiDataType_Float, const_cast<float*>(&v.m10_), 4, floatStep, &floatMin, &floatMax, "%.3f");
                modified |= ui::DragScalarN("###mm2", ImGuiDataType_Float, const_cast<float*>(&v.m20_), 4, floatStep, &floatMin, &floatMax, "%.3f");
                modified |= ui::DragScalarN("###mm3", ImGuiDataType_Float, const_cast<float*>(&v.m30_), 4, floatStep, &floatMin, &floatMax, "%.3f");
            }
            ui::EndGroup();
            break;
        }
        case VAR_DOUBLE:
        {
            auto v = value.GetDouble();
            modified |= ui::DragScalar("", ImGuiDataType_Double, &v, floatStep, &doubleMin, &doubleMax, "%.3f");
            if (modified)
                value = v;
            break;
        }
        case VAR_STRINGVECTOR:
        {
            StringVector* v = value.GetStringVectorPtr();
            // Insert new item.
            {
                auto* buffer = ui::GetUIState<ea::string>();
                modified = ui::InputTextWithHint("", "Enter text and press [Enter]", buffer, ImGuiInputTextFlags_EnterReturnsTrue);
                if (modified)
                {
                    v->push_back(*buffer);
                    buffer->clear();
                    ui::RemoveUIState<ea::string>();
                }
            }
            // List of current items.
            StringVector& buffers = *ui::GetUIState<StringVector>();
            buffers.resize(v->size());
            for (int i = 0; i < v->size();)
            {
                ea::string& sv = v->at(i);
                ea::string& buffer = buffers[i];
                // Initial value for temporary buffer that will be edited. Committing value will save it to sv.
                if (buffer.empty() && buffer != sv)
                    buffer = sv;

                bool dirty = sv != buffer;
                if (dirty)
                    ui::PushStyleColor(ImGuiCol_Text, ui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                // Input widget of one item.
                ui::SetNextItemWidth(ui::CalcItemWidth() - ui::IconButtonSize() - style.ItemSpacing.x);
                if (ui::InputTextWithHint(Format("[{}]", i).c_str(), "Enter value and press [Enter]",
                    &buffer, ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    sv = buffer;
                    modified = true;
                }
                if (dirty)
                {
                    ui::PopStyleColor();
                    if (ui::IsItemHovered())
                        ui::SetTooltip("Press [Enter] to commit changes.");
                }
                // Delete button.
                ui::SameLine();
                bool deleted = ui::IconButton(ICON_FA_TRASH);
                if (deleted)
                {
                    v->erase_at(i);
                    buffers.erase_at(i);                    // Remove temporary buffer of deleted item.
                    modified = true;
                }
                else
                    ++i;
            }
            // Separate array of items from following attributes.
            if (!v->empty())
                ui::Separator();
            break;
        }
        case VAR_RECT:
        {
            auto& v = value.GetRect();
            const char* formats[] = {"MinX=%.3f", "MinY=%.3f", "MaxX=%.3f", "MaxY=%.3f"};
            if (showHelperLabels)
                modified |= ui::DragScalarFormatsN("", ImGuiDataType_Float, const_cast<float*>(&v.min_.x_), 4, floatStep, &floatMin, &floatMax, formats);
            else
                modified |= ui::DragScalarN("", ImGuiDataType_Float, const_cast<float*>(&v.min_.x_), 4, floatStep, &floatMin, &floatMax, "%.3f");
            break;
        }
        case VAR_INTVECTOR3:
        {
            auto& v = value.GetIntVector3();
            const char* formats[] = {"X=%d", "Y=%d", "Z=%d"};
            if (showHelperLabels)
                modified |= ui::DragScalarFormatsN("", ImGuiDataType_S32, const_cast<int*>(&v.x_), 3, 1, &intMin, &intMax, formats, 1.0f);
            else
                modified |= ui::DragScalarN("", ImGuiDataType_S32, const_cast<int*>(&v.x_), 3, 1, &intMin, &intMax, "%d", 1.0f);
            break;
        }
        case VAR_INT64:
        {
            auto minVal = std::numeric_limits<long long int>::min();
            auto maxVal = std::numeric_limits<long long int>::max();
            auto v = value.GetInt64();
            modified |= ui::DragScalar("", ImGuiDataType_S64, &v, 1, &minVal, &maxVal);
            if (modified)
                value = v;
            break;
        }
        default:
            break;
        }
    }

    if (item_width != 0)
        ui::PopItemWidth();

    if (openAttributeMenu)
        ui::OpenPopup("Attribute Menu");

    return modified;
}

bool RenderAttributes(Serializable* item, ea::string_view filter, Object* eventSender)
{
    if (item->GetNumAttributes() == 0)
        return false;

    if (eventSender == nullptr)
        eventSender = item;

    const ea::vector<AttributeInfo>* attributes = item->GetAttributes();
    if (attributes == nullptr)
        return false;

    ui::IdScope itemId(item);
    bool modifiedAny = false;

    for (const AttributeInfo& info: *attributes)
    {
        // Attribute is not meant to be edited in editor.
        if (info.mode_ & AM_NOEDIT)
            continue;
        // Ignore attributes not matching user-provided filter.
        if (!filter.empty() && !info.name_.contains(filter, false))
            continue;
        // Ignore not supported variant types.
        if (info.type_ == VAR_BUFFER || info.type_ == VAR_VARIANTVECTOR || info.type_ == VAR_VOIDPTR || info.type_ == VAR_PTR)
            continue;
        ui::IdScope attributeNameId(info.name_.c_str());

        Color color = Color::TRANSPARENT_BLACK;                                 // No change, determine automatically later.
        auto& modification = ValueHistory<Variant>::Get(item->GetAttribute(info.name_));
        Variant& value = modification.current_;

        AttributeValueKind valueKind = AttributeValueKind::ATTRIBUTE_VALUE_CUSTOM;
        Variant inheritedDefault = item->GetInstanceDefault(info.name_);
        if (!inheritedDefault.IsEmpty() && value == inheritedDefault)
            valueKind = AttributeValueKind::ATTRIBUTE_VALUE_INHERITED;
        else if (value == info.defaultValue_)
            valueKind = AttributeValueKind::ATTRIBUTE_VALUE_DEFAULT;
        else if (info.type_ == VAR_RESOURCEREFLIST)
        {
            // Model insists on keeping non-empty ResourceRefList of Materials, even when names are unset. Treat such
            // list with empty names as equal to default empty resource reflist.
            const ResourceRefList& defaultList = info.defaultValue_.GetResourceRefList();
            const ResourceRefList& valueList = value.GetResourceRefList();
            bool allEmpty = defaultList.names_.empty();
            for (const auto& name : valueList.names_)
                allEmpty &= name.empty();
            if (allEmpty && defaultList.type_ == valueList.type_)
                valueKind = AttributeValueKind::ATTRIBUTE_VALUE_DEFAULT;
        }

        // Customize attribute rendering
        const ea::string* tooltip = &EMPTY_STRING;
        {
            using namespace AttributeInspectorAttribute;
            VariantMap& args = eventSender->GetEventDataMap();
            args[P_SERIALIZABLE] = item;
            args[P_ATTRIBUTEINFO] = (void*)&info;
            args[P_COLOR] = color;
            args[P_HIDDEN] = false;
            args[P_TOOLTIP] = "";
            args[P_VALUE_KIND] = (int)valueKind;
            eventSender->SendEvent(E_ATTRIBUTEINSPECTOATTRIBUTE, args);
            if (args[P_HIDDEN].GetBool())
                continue;
            color = args[P_COLOR].GetColor();
            valueKind = (AttributeValueKind)args[P_VALUE_KIND].GetInt();
            tooltip = &args[P_TOOLTIP].GetString();
        }

        if (color == Color::TRANSPARENT_BLACK)
        {
            switch (valueKind)
            {
            case AttributeValueKind::ATTRIBUTE_VALUE_INHERITED:
                color = Color::GREEN;
                break;
            case AttributeValueKind::ATTRIBUTE_VALUE_CUSTOM:
                color = Color::WHITE;
                break;
            case AttributeValueKind::ATTRIBUTE_VALUE_DEFAULT:
            default:
                color = Color::GRAY;
            }
        }

        AttributeInspectorModified modifiedReason = AttributeInspectorModified::NO_CHANGE;
        bool modified = RenderAttribute(info.name_, value, color, *tooltip, &info, eventSender, 0);

        if (ui::BeginPopup("Attribute Menu"))
        {
            auto& style = ui::GetStyle();
            if (!info.defaultValue_.IsEmpty())
            {
                if (valueKind == AttributeValueKind::ATTRIBUTE_VALUE_DEFAULT)
                {
                    ui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                    ui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
                }
                if (ui::MenuItem("Reset to default"))
                {
                    value = info.defaultValue_;
                    modified = true;
                    modifiedReason = AttributeInspectorModified::SET_DEFAULT;
                }
                if (valueKind == AttributeValueKind::ATTRIBUTE_VALUE_DEFAULT)
                {
                    ui::PopStyleColor();
                    ui::PopItemFlag();
                }
            }
            if (!inheritedDefault.IsEmpty())
            {
                if (valueKind == AttributeValueKind::ATTRIBUTE_VALUE_INHERITED)
                {
                    ui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                    ui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
                }
                if (ui::MenuItem("Reset to inherited"))
                {
                    value = inheritedDefault;     // For current frame to render correctly
                    modified = true;
                    modifiedReason = AttributeInspectorModified::SET_INHERITED;
                }
                if (valueKind == AttributeValueKind::ATTRIBUTE_VALUE_INHERITED)
                {
                    ui::PopStyleColor();
                    ui::PopItemFlag();
                }
            }

            if (value.GetType() == VAR_INT && info.name_.ends_with(" Mask"))
            {
                if (ui::MenuItem("Enable All"))
                {
                    value = M_MAX_UNSIGNED;
                    modified = true;
                }
                if (ui::MenuItem("Disable All"))
                {
                    value = 0;
                    modified = true;
                }
                if (ui::MenuItem("Toggle"))
                {
                    value = value.GetUInt() ^ M_MAX_UNSIGNED;
                    modified = true;
                }
            }

            // Allow customization of attribute menu.
            using namespace AttributeInspectorMenu;
            eventSender->SendEvent(E_ATTRIBUTEINSPECTORMENU, P_SERIALIZABLE, item, P_ATTRIBUTEINFO, (void*)&info);

            ImGui::EndPopup();
        }

        if (modified)
        {
            if (modifiedReason == AttributeInspectorModified::NO_CHANGE)
                modifiedReason = AttributeInspectorModified::SET_BY_USER;

            // Discard temporary string buffer so input field clears.
            if (value.GetType() == VAR_STRING)
                ui::RemoveUIState<ea::string>();

            modification.SetModified(true);
            item->SetAttribute(info.name_, value);
            item->ApplyAttributes();
        }

        if (modification.IsModified() || modifiedReason != AttributeInspectorModified::NO_CHANGE)
        {
            // This attribute was modified on last frame, but not on this frame. Continuous attribute value modification
            // has ended and we can fire attribute modification event.
            using namespace AttributeInspectorValueModified;
            VariantMap& args = eventSender->GetEventDataMap();
            args[P_SERIALIZABLE] = item;
            args[P_ATTRIBUTEINFO] = (void*)&info;
            args[P_OLDVALUE] = modification.initial_;
            args[P_NEWVALUE] = modification.current_;
            args[P_REASON] = (unsigned)modifiedReason;
            eventSender->SendEvent(E_ATTRIBUTEINSPECTVALUEMODIFIED, args);
            modifiedAny |= true;    // Should not early-return because it would not render entire attribute list and cause
                                    // flickering and unintended scrolling of attribute list.
        }
    }
    return modifiedAny;
}

}
