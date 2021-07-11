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

#include "../IO/Log.h"
#include "../Graphics/GraphicsDefs.h"

#include <EASTL/span.h>
#include <EASTL/vector.h>

namespace Urho3D
{

/// Reference to the region in constant buffer within collection. Plain old data.
struct ConstantBufferCollectionRef
{
    /// Index of buffer in collection.
    unsigned index_;
    /// Offset in the buffer.
    unsigned offset_;
    /// Size of the chunk.
    unsigned size_;
};

/// Buffer of shader parameters ready to be uploaded.
class ConstantBufferCollection
{
public:
    /// Clear and/or initialize for work.
    void ClearAndInitialize(unsigned alignment)
    {
        alignment_ = alignment;
        currentBufferIndex_ = 0;
        for (auto& buffer : buffers_)
            buffer.second = 0;

        if (buffers_.empty())
            AllocateBuffer();
    }

    /// Allocate new block.
    ea::pair<ConstantBufferCollectionRef, unsigned char*> AddBlock(unsigned size)
    {
        assert(size <= bufferSize_);
        const unsigned alignedSize = (size + alignment_ - 1) / alignment_ * alignment_;

        if (bufferSize_ - buffers_[currentBufferIndex_].second < alignedSize)
        {
            ++currentBufferIndex_;
            if (buffers_.size() <= currentBufferIndex_)
                AllocateBuffer();
        }

        auto& currentBuffer = buffers_[currentBufferIndex_];
        const unsigned offset = currentBuffer.second;
        currentBuffer.second += alignedSize;

        unsigned char* data = &currentBuffer.first[offset];
        return {{ currentBufferIndex_, offset, size }, data };
    }

    /// Return number of buffers.
    unsigned GetNumBuffers() const { return currentBufferIndex_ + 1; }

    /// Return used size of the CPU buffer.
    unsigned GetBufferSize(unsigned index) const
    {
        return buffers_[index].second;
    }

    /// Return best size of GPU buffer. Round up to next power of two.
    unsigned GetGPUBufferSize(unsigned index) const
    {
        return ea::max(256u, NextPowerOfTwo(GetBufferSize(index)));
    }

    /// Return buffer data.
    const void* GetBufferData(unsigned index) const { return buffers_[index].first.data(); }

    /// Copy variant parameter into storage.
    static bool StoreParameter(unsigned char* dest, unsigned size, const Variant& value)
    {
        switch (value.GetType())
        {
        case VAR_BOOL:
            return StoreParameter(dest, size, value.GetBool() ? 1 : 0);

        case VAR_INT:
            return StoreParameter(dest, size, value.GetInt());

        case VAR_FLOAT:
        case VAR_DOUBLE:
            return StoreParameter(dest, size, value.GetFloat());

        case VAR_VECTOR2:
            return StoreParameter(dest, size, value.GetVector2());

        case VAR_VECTOR3:
            return StoreParameter(dest, size, value.GetVector3());

        case VAR_VECTOR4:
            return StoreParameter(dest, size, value.GetVector4());

        case VAR_COLOR:
            return StoreParameter(dest, size, value.GetColor());

        case VAR_MATRIX3:
            return StoreParameter(dest, size, value.GetMatrix3());

        case VAR_MATRIX3X4:
            return StoreParameter(dest, size, value.GetMatrix3x4());

        case VAR_MATRIX4:
            return StoreParameter(dest, size, value.GetMatrix4());

        default:
            // Unsupported parameter type, do nothing
            return false;
        }
    }

    /// Copy new simple parameter into storage. Trim if too much data provided.
    template <class T>
    static bool StoreParameter(unsigned char* dest, unsigned size, const T& value)
    {
        if (size > sizeof(T))
            return false;
        memcpy(dest, &value, size);
        return true;
    }

    /// Copy new Matrix3 parameter into storage.
    static bool StoreParameter(unsigned char* dest, unsigned size, const Matrix3& value)
    {
        if (size != 11 * sizeof(float))
            return false;

        const Matrix3x4 data{ value };
        memcpy(dest, data.Data(), sizeof(Matrix3x4));
        return true;
    }

    /// Copy new Matrix3x4 parameter into storage.
    static bool StoreParameter(unsigned char* dest, unsigned size, const Matrix3x4& value)
    {
        if (size != 12 * sizeof(float) && size != 16 * sizeof(float))
            return false;

        memcpy(dest, value.ToMatrix4().Data(), size);
        return true;
    }

    /// Add new Vector4 array parameter.
    static bool StoreParameter(unsigned char* dest, unsigned size, ea::span<const Vector4> values)
    {
        if (size < 4 * sizeof(float) * values.size())
            return false;

        memcpy(dest, values.data(), sizeof(Vector4) * values.size());
        return true;
    }

    /// Add new Matrix3x4 array parameter.
    static bool StoreParameter(unsigned char* dest, unsigned size, ea::span<const Matrix3x4> values)
    {
        if (size < 12 * sizeof(float) * values.size())
            return false;

        memcpy(dest, values.data(), sizeof(Matrix3x4) * values.size());
        return true;
    }

    /// Add new Matrix4 array parameter.
    static bool StoreParameter(unsigned char* dest, unsigned size, ea::span<const Matrix4> values)
    {
        if (size < 16 * sizeof(float) * values.size())
            return false;

        memcpy(dest, values.data(), sizeof(Matrix4) * values.size());
        return true;
    }

private:
    /// Allocate one more buffer.
    void AllocateBuffer()
    {
        buffers_.emplace_back();
        buffers_.back().first.resize(bufferSize_);
    }

    /// Size of buffers.
    unsigned bufferSize_{ 16384 };
    /// Alignment of each block.
    unsigned alignment_{ 1 };
    /// Buffers.
    ea::vector<ea::pair<ByteVector, unsigned>> buffers_;
    /// Current buffer index.
    unsigned currentBufferIndex_{};
};

}
