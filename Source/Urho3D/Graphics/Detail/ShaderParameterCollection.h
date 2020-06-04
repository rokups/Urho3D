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

#include "../../Graphics/Graphics.h"

#include <EASTL/span.h>
#include <EASTL/vector.h>

namespace Urho3D
{

/// Collection of shader parameters.
class ShaderParameterCollection
{
public:
    /// Return next parameter offset.
    unsigned GetNextParameterOffset() const
    {
        return names_.size();
    }

    /// Add new variant parameter.
    void AddParameter(StringHash name, const Variant& value)
    {
        switch (value.GetType())
        {
        case VAR_BOOL:
            AddParameter(name, value.GetBool() ? 1 : 0);
            break;

        case VAR_INT:
            AddParameter(name, value.GetInt());
            break;

        case VAR_FLOAT:
        case VAR_DOUBLE:
            AddParameter(name, value.GetFloat());
            break;

        case VAR_VECTOR2:
            AddParameter(name, value.GetVector2());
            break;

        case VAR_VECTOR3:
            AddParameter(name, value.GetVector3());
            break;

        case VAR_VECTOR4:
            AddParameter(name, value.GetVector4());
            break;

        case VAR_COLOR:
            AddParameter(name, value.GetColor());
            break;

        case VAR_MATRIX3:
            AddParameter(name, value.GetMatrix3());
            break;

        case VAR_MATRIX3X4:
            AddParameter(name, value.GetMatrix3x4());
            break;

        case VAR_MATRIX4:
            AddParameter(name, value.GetMatrix4());
            break;

        default:
            // Unsupported parameter type, do nothing
            break;
        }
    }

    /// Add new int parameter.
    void AddParameter(StringHash name, int value)
    {
        const int data[4]{ value, 0, 0, 0 };
        AllocateParameter(name, VAR_INTRECT, 1, data, 4);
    }

    /// Add new float parameter.
    void AddParameter(StringHash name, float value)
    {
        const Vector4 data{ value, 0.0f, 0.0f, 0.0f };
        AllocateParameter(name, VAR_VECTOR4, 1, data.Data(), 4);
    }

    /// Add new Vector2 parameter.
    void AddParameter(StringHash name, const Vector2& value)
    {
        const Vector4 data{ value.x_, value.y_, 0.0f, 0.0f };
        AllocateParameter(name, VAR_VECTOR4, 1, data.Data(), 4);
    }

    /// Add new Vector3 parameter.
    void AddParameter(StringHash name, const Vector3& value)
    {
        const Vector4 data{ value, 0.0f };
        AllocateParameter(name, VAR_VECTOR4, 1, data.Data(), 4);
    }

    /// Add new Vector4 parameter.
    void AddParameter(StringHash name, const Vector4& value)
    {
        AllocateParameter(name, VAR_VECTOR4, 1, value.Data(), 4);
    }

    /// Add new Color parameter.
    void AddParameter(StringHash name, const Color& value)
    {
        AllocateParameter(name, VAR_VECTOR4, 1, value.Data(), 4);
    }

    /// Add new Matrix3 parameter.
    void AddParameter(StringHash name, const Matrix3& value)
    {
        const Matrix3x4 data{ value };
        AllocateParameter(name, VAR_MATRIX3X4, 1, data.Data(), 12);
    }

    /// Add new Matrix3x4 parameter.
    void AddParameter(StringHash name, const Matrix3x4& value)
    {
        AllocateParameter(name, VAR_MATRIX3X4, 1, value.Data(), 12);
    }

    /// Add new Matrix4 parameter.
    void AddParameter(StringHash name, const Matrix4& value)
    {
        AllocateParameter(name, VAR_MATRIX4, 1, value.Data(), 16);
    }

    /// Add new Vector4 array parameter.
    void AddParameter(StringHash name, ea::span<const Vector4> values)
    {
        AllocateParameter(name, VAR_VECTOR4, values.size(), values.data()->Data(), 4 * values.size());
    }

    /// Clear.
    void Clear()
    {
        count_ = 0;
        offset_ = 0;
    }

    /// Return size.
    unsigned Size() const { return count_; }

    /// Iterate subset.
    template <class T>
    void ForEach(unsigned from, unsigned to, const T& callback) const
    {
        const unsigned char* dataPointer = data_.data();
        for (unsigned i = from; i < to; ++i)
        {
            const void* rawData = &dataPointer[dataOffsets_[i]];
            const StringHash name = names_[i];
            const VariantType type = dataTypes_[i];
            const unsigned arraySize = dataSizes_[i];
            switch (type)
            {
            case VAR_INTRECT:
            {
                const auto data = reinterpret_cast<const int*>(rawData);
                callback(name, data, arraySize);
                break;
            }
            case VAR_VECTOR4:
            {
                const auto data = reinterpret_cast<const Vector4*>(rawData);
                callback(name, data, arraySize);
                break;
            }
            case VAR_MATRIX3X4:
            {
                const auto data = reinterpret_cast<const Matrix3x4*>(rawData);
                callback(name, data, arraySize);
                break;
            }
            case VAR_MATRIX4:
            {
                const auto data = reinterpret_cast<const Matrix4*>(rawData);
                callback(name, data, arraySize);
                break;
            }
            default:
                break;
            }
        }
    }

    /// Iterate all.
    template <class T>
    void ForEach(const T& callback) const
    {
        ForEach(0, Size(), callback);
    }

private:
    /// Add new parameter.
    template <class T>
    void AllocateParameter(StringHash name, VariantType type, unsigned arraySize, const T* srcData, unsigned count)
    {
        static const unsigned alignment = 4 * sizeof(float);
        const unsigned alignedSize = (count * sizeof(T) + alignment - 1) / alignment * alignment;

        // Resize data buffer
        const unsigned dataSize = data_.size();
        if (offset_ + alignedSize > dataSize)
            data_.resize(dataSize > 0 ? dataSize * 2 + alignedSize : 64);

        // Resize metadata buffers
        const unsigned metadataSize = names_.size();
        if (count_ + 1 > metadataSize)
        {
            const unsigned newMetadataSize = metadataSize > 0 ? metadataSize * 2 : 16;
            names_.resize(newMetadataSize);
            dataOffsets_.resize(newMetadataSize);
            dataSizes_.resize(newMetadataSize);
            dataTypes_.resize(newMetadataSize);
        }

        // Store metadata
        names_[count_] = name;
        dataOffsets_[count_] = offset_;
        dataSizes_[count_] = arraySize;
        dataTypes_[count_] = type;

        memcpy(&data_[offset_], srcData, count * sizeof(T));

        offset_ += alignedSize;
        ++count_;
    }

    /// Parameter names.
    ea::vector<StringHash> names_;
    /// Parameter offsets in data buffer.
    ea::vector<unsigned> dataOffsets_;
    /// Parameter array sizes in data buffer.
    ea::vector<unsigned> dataSizes_;
    /// Parameter types in data buffer.
    ea::vector<VariantType> dataTypes_;
    /// Number of variables in the buffer.
    unsigned count_{};
    /// Offset in data buffer.
    unsigned offset_{};
    /// Data buffer.
    ByteVector data_;
};

/// Functor that applies shader parameter depending on its type.
struct SharedParameterSetter
{
    /// Graphics.
    Graphics* graphics_;
    /// Apply array of int vectors. Not fully supported.
    void operator()(const StringHash& name, const int* data, unsigned arraySize) const
    {
        graphics_->SetShaderParameter(name, *data);
    }
    /// Apply array of float vectors.
    void operator()(const StringHash& name, const Vector4* data, unsigned arraySize) const
    {
        if (arraySize != 1)
            graphics_->SetShaderParameter(name, data->Data(), 4 * arraySize);
        else
            graphics_->SetShaderParameter(name, *data);
    }
    /// Apply array of 3x4 matrices.
    void operator()(const StringHash& name, const Matrix3x4* data, unsigned arraySize) const
    {
        if (arraySize != 1)
            graphics_->SetShaderParameter(name, data->Data(), 12 * arraySize);
        else
            graphics_->SetShaderParameter(name, *data);
    }
    /// Apply array of 4x4 matrices.
    void operator()(const StringHash& name, const Matrix4* data, unsigned arraySize) const
    {
        if (arraySize != 1)
            graphics_->SetShaderParameter(name, data->Data(), 16 * arraySize);
        else
            graphics_->SetShaderParameter(name, *data);
    }
};

}
