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

#pragma once

#include <EASTL/shared_array.h>

#include "../Core/Object.h"
#include "../Graphics/GPUObject.h"
#include "../Graphics/GraphicsDefs.h"
#include "../Graphics/PipelineStateTracker.h"

namespace Urho3D
{

/// Type of index buffer.
enum IndexBufferType
{
    IBT_NONE = 0,
    IBT_UINT16,
    IBT_UINT32
};

/// Hardware index buffer.
class URHO3D_API IndexBuffer : public Object, public GPUObject, public PipelineStateTracker
{
    URHO3D_OBJECT(IndexBuffer, Object);

    using GPUObject::GetGraphics;

public:
    /// Construct. Optionally force headless (no GPU-side buffer) operation.
    explicit IndexBuffer(Context* context, bool forceHeadless = false);
    /// Destruct.
    ~IndexBuffer() override;

    /// Register object with the engine.
    static void RegisterObject(Context* context);

    /// Mark the buffer destroyed on graphics context destruction. May be a no-op depending on the API.
    void OnDeviceLost() override;
    /// Recreate the buffer and restore data if applicable. May be a no-op depending on the API.
    void OnDeviceReset() override;
    /// Release buffer.
    void Release() override;

    /// Enable shadowing in CPU memory. Shadowing is forced on if the graphics subsystem does not exist.
    /// @property
    void SetShadowed(bool enable);
    /// Set size and vertex elements and dynamic mode. Previous data will be lost.
    bool SetSize(unsigned indexCount, bool largeIndices, bool dynamic = false);
    /// Set all data in the buffer.
    bool SetData(const void* data);
    /// Set a data range in the buffer. Optionally discard data outside the range.
    bool SetDataRange(const void* data, unsigned start, unsigned count, bool discard = false);
    /// Lock the buffer for write-only editing. Return data pointer if successful. Optionally discard data outside the range.
    void* Lock(unsigned start, unsigned count, bool discard = false);
    /// Unlock the buffer and apply changes to the GPU buffer.
    void Unlock();

    /// Return whether CPU memory shadowing is enabled.
    /// @property
    bool IsShadowed() const { return shadowed_; }

    /// Return whether is dynamic.
    /// @property
    bool IsDynamic() const { return dynamic_; }

    /// Return whether is currently locked.
    bool IsLocked() const { return lockState_ != LOCK_NONE; }

    /// Return number of indices.
    /// @property
    unsigned GetIndexCount() const { return indexCount_; }

    /// Return index size in bytes.
    /// @property
    unsigned GetIndexSize() const { return indexSize_; }

    /// Return used vertex range from index range.
    bool GetUsedVertexRange(unsigned start, unsigned count, unsigned& minVertex, unsigned& vertexCount);

    /// Return CPU memory shadow data.
    unsigned char* GetShadowData() const { return shadowData_.get(); }

    /// Return shared array pointer to the CPU memory shadow data.
    ea::shared_array<unsigned char> GetShadowDataShared() const { return shadowData_; }

    /// Return unpacked buffer data as plain array of indices.
    ea::vector<unsigned> GetUnpackedData(unsigned start = 0, unsigned count = M_MAX_UNSIGNED) const;

    /// Set data in the buffer from unpacked data. Data should contain at least `count` elements.
    void SetUnpackedData(const unsigned data[], unsigned start = 0, unsigned count = M_MAX_UNSIGNED);

    /// Unpack index data from index buffer into unsigned int array.
    static void UnpackIndexData(const void* source, bool largeIndices, unsigned start, unsigned count, unsigned dest[]);

    /// Pack index data from unsigned int array into index buffer.
    static void PackIndexData(const unsigned source[], void* dest, bool largeIndices, unsigned start, unsigned count);

    /// Return type of index buffer. Null is allowed.
    static IndexBufferType GetIndexBufferType(IndexBuffer* indexBuffer)
    {
        if (!indexBuffer)
            return IBT_NONE;
        const bool largeIndices = indexBuffer->GetIndexSize() == 4;
        return largeIndices ? IBT_UINT32 : IBT_UINT16;
    }

private:
    /// Create buffer.
    bool Create();
    /// Update the shadow data to the GPU buffer.
    bool UpdateToGPU();
    /// Map the GPU buffer into CPU memory. Not used on OpenGL.
    void* MapBuffer(unsigned start, unsigned count, bool discard);
    /// Unmap the GPU buffer. Not used on OpenGL.
    void UnmapBuffer();

    /// Recalculate hash (must not be non zero). Shall be save to call from multiple threads as long as the object is not changing.
    unsigned RecalculatePipelineStateHash() const override;

    /// Shadow data.
    ea::shared_array<unsigned char> shadowData_;
    /// Number of indices.
    unsigned indexCount_;
    /// Index size.
    unsigned indexSize_;
    /// Buffer locking state.
    LockState lockState_;
    /// Lock start vertex.
    unsigned lockStart_;
    /// Lock number of vertices.
    unsigned lockCount_;
    /// Scratch buffer for fallback locking.
    void* lockScratchData_;
    /// Dynamic flag.
    bool dynamic_;
    /// Shadowed flag.
    bool shadowed_;
    /// Discard lock flag. Used by OpenGL only.
    bool discardLock_;
};

/// Index Buffer of dynamic size. Resize policy is similar to standard vector.
class URHO3D_API DynamicIndexBuffer : public Object
{
    URHO3D_OBJECT(DynamicIndexBuffer, Object);

public:
    DynamicIndexBuffer(Context* context);
    bool Initialize(unsigned indexCount, bool largeIndices);

    /// Discard existing content of the buffer.
    void Discard();
    /// Commit all added data to GPU.
    void Commit();

    /// Allocate indices. Returns index of first index and writeable buffer of sufficient size.
    ea::pair<unsigned, unsigned char*> AddIndices(unsigned numIndices)
    {
        const unsigned startIndex = numIndices_;
        if (startIndex + numIndices > maxNumIndices_)
            GrowBuffer();

        numIndices_ += numIndices;
        unsigned char* data = shadowData_.data() + startIndex * indexSize_;
        return { startIndex, data };
    }

    /// Store indices. Returns index of first vertex.
    unsigned AddIndices(unsigned count, const void* data)
    {
        const auto indexAndData = AddIndices(count);
        memcpy(indexAndData.second, data, count * indexSize_);
        return indexAndData.first;
    }

    IndexBuffer* GetIndexBuffer() { return indexBuffer_; }

private:
    void GrowBuffer();

    SharedPtr<IndexBuffer> indexBuffer_;
    ByteVector shadowData_;
    bool indexBufferNeedResize_{};

    unsigned indexSize_{};
    unsigned numIndices_{};
    unsigned maxNumIndices_{};
};

}
