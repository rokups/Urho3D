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

    /// Return size of the buffer.
    unsigned GetBufferSize(unsigned index) const { return bufferSize_; }

    /// Return buffer data.
    const void* GetBufferData(unsigned index) const { return buffers_[index].first.data(); }

    /// Copy variant parameter into storage.
    static void StoreParameter(unsigned char* dest, const Variant& value)
    {
        switch (value.GetType())
        {
        case VAR_BOOL:
            StoreParameter(dest, value.GetBool() ? 1 : 0);
            break;

        case VAR_INT:
            StoreParameter(dest, value.GetInt());
            break;

        case VAR_FLOAT:
        case VAR_DOUBLE:
            StoreParameter(dest, value.GetFloat());
            break;

        case VAR_VECTOR2:
            StoreParameter(dest, value.GetVector2());
            break;

        case VAR_VECTOR3:
            StoreParameter(dest, value.GetVector3());
            break;

        case VAR_VECTOR4:
            StoreParameter(dest, value.GetVector4());
            break;

        case VAR_COLOR:
            StoreParameter(dest, value.GetColor());
            break;

        case VAR_MATRIX3:
            StoreParameter(dest, value.GetMatrix3());
            break;

        case VAR_MATRIX3X4:
            StoreParameter(dest, value.GetMatrix3x4());
            break;

        case VAR_MATRIX4:
            StoreParameter(dest, value.GetMatrix4());
            break;

        default:
            // Unsupported parameter type, do nothing
            break;
        }
    }

    /// Copy new simple parameter into storage.
    template <class T>
    static void StoreParameter(unsigned char* dest, const T& value)
    {
        memcpy(dest, &value, sizeof(T));
    }

    /// Copy new Matrix3 parameter into storage.
    static void StoreParameter(unsigned char* dest, const Matrix3& value)
    {
        const Matrix3x4 data{ value };
        memcpy(dest, data.Data(), sizeof(Matrix3x4));
    }

    /// Add new Vector4 array parameter.
    static void StoreParameter(unsigned char* dest, ea::span<const Vector4> values)
    {
        memcpy(dest, values.data(), sizeof(Vector4) * values.size());
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
