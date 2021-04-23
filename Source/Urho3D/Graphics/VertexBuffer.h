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

#include "../Container/ByteVector.h"
#include "../Core/Object.h"
#include "../Graphics/GPUObject.h"
#include "../Graphics/GraphicsDefs.h"
#include "../Graphics/PipelineStateTracker.h"
#include "../Math/Vector4.h"

namespace Urho3D
{

/// Hardware vertex buffer.
class URHO3D_API VertexBuffer : public Object, public GPUObject, public PipelineStateTracker
{
    URHO3D_OBJECT(VertexBuffer, Object);

    using GPUObject::GetGraphics;

public:
    /// Construct. Optionally force headless (no GPU-side buffer) operation.
    explicit VertexBuffer(Context* context, bool forceHeadless = false);
    /// Destruct.
    ~VertexBuffer() override;

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
    /// Set size, vertex elements and dynamic mode. Previous data will be lost.
    bool SetSize(unsigned vertexCount, const ea::vector<VertexElement>& elements, bool dynamic = false);
    /// Set size and vertex elements and dynamic mode using legacy element bitmask. Previous data will be lost.
    bool SetSize(unsigned vertexCount, unsigned elementMask, bool dynamic = false);
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

    /// Return number of vertices.
    /// @property
    unsigned GetVertexCount() const { return vertexCount_; }

    /// Return vertex size in bytes.
    /// @property
    unsigned GetVertexSize() const { return vertexSize_; }

    /// Return vertex elements.
    /// @property
    const ea::vector<VertexElement>& GetElements() const { return elements_; }

    /// Return vertex element, or null if does not exist.
    const VertexElement* GetElement(VertexElementSemantic semantic, unsigned char index = 0) const;

    /// Return vertex element with specific type, or null if does not exist.
    const VertexElement* GetElement(VertexElementType type, VertexElementSemantic semantic, unsigned char index = 0) const;

    /// Return whether has a specified element semantic.
    bool HasElement(VertexElementSemantic semantic, unsigned char index = 0) const { return GetElement(semantic, index) != nullptr; }

    /// Return whether has an element semantic with specific type.
    bool HasElement(VertexElementType type, VertexElementSemantic semantic, unsigned char index = 0) const { return GetElement(type, semantic, index) != nullptr; }

    /// Return offset of a element within vertex, or M_MAX_UNSIGNED if does not exist.
    unsigned GetElementOffset(VertexElementSemantic semantic, unsigned char index = 0) const { const VertexElement* element = GetElement(semantic, index); return element ? element->offset_ : M_MAX_UNSIGNED; }

    /// Return offset of a element with specific type within vertex, or M_MAX_UNSIGNED if element does not exist.
    unsigned GetElementOffset(VertexElementType type, VertexElementSemantic semantic, unsigned char index = 0) const { const VertexElement* element = GetElement(type, semantic, index); return element ? element->offset_ : M_MAX_UNSIGNED; }

    /// Return legacy vertex element mask. Note that both semantic and type must match the legacy element for a mask bit to be set.
    /// @property
    VertexMaskFlags GetElementMask() const { return elementMask_; }

    /// Return CPU memory shadow data.
    unsigned char* GetShadowData() const { return shadowData_.get(); }

    /// Return shared array pointer to the CPU memory shadow data.
    ea::shared_array<unsigned char> GetShadowDataShared() const { return shadowData_; }

    /// Return buffer hash for building vertex declarations. Used internally.
    unsigned long long GetBufferHash(unsigned streamIndex) { return elementHash_ << (streamIndex * 16); }

    /// Return unpacked buffer data as `count * elements.size()` elements, grouped by vertices.
    ea::vector<Vector4> GetUnpackedData(unsigned start = 0, unsigned count = M_MAX_UNSIGNED) const;

    /// Set data in the buffer from unpacked data.
    void SetUnpackedData(const Vector4 data[], unsigned start = 0, unsigned count = M_MAX_UNSIGNED);

    /// Return element with specified type and semantic from a vertex element list, or null if does not exist.
    static const VertexElement* GetElement(const ea::vector<VertexElement>& elements, VertexElementType type, VertexElementSemantic semantic, unsigned char index = 0);

    /// Return whether element list has a specified element type and semantic.
    static bool HasElement(const ea::vector<VertexElement>& elements, VertexElementType type, VertexElementSemantic semantic, unsigned char index = 0);

    /// Return element offset for specified type and semantic from a vertex element list, or M_MAX_UNSIGNED if does not exist.
    static unsigned GetElementOffset(const ea::vector<VertexElement>& elements, VertexElementType type, VertexElementSemantic semantic, unsigned char index = 0);

    /// Return a vertex element list from a legacy element bitmask.
    static ea::vector<VertexElement> GetElements(unsigned elementMask);

    /// Return vertex size from an element list.
    static unsigned GetVertexSize(const ea::vector<VertexElement>& elements);

    /// Return vertex size for a legacy vertex element bitmask.
    static unsigned GetVertexSize(unsigned elementMask);

    /// Update offsets of vertex elements.
    static void UpdateOffsets(ea::vector<VertexElement>& elements);

    /// Unpack given element data from vertex buffer into Vector4. Stride is measured in bytes.
    static void UnpackVertexData(const void* source, unsigned sourceStride, const VertexElement& element, unsigned start, unsigned count, Vector4 dest[], unsigned destStride);

    /// Pack given element data from Vector4 into vertex buffer. Stride is measured in bytes.
    static void PackVertexData(const Vector4 source[], unsigned sourceStride, void* dest, unsigned destStride, const VertexElement& element, unsigned start, unsigned count);

    /// Shuffle unpacked vertex data according to another vertex format. Element types are ignored. Source array should have `vertexCount * sourceElements.size()` elements. Destination array should have `vertexCount * destElements.size()` elements.
    static void ShuffleUnpackedVertexData(unsigned vertexCount, const Vector4 source[], const ea::vector<VertexElement>& sourceElements, Vector4 dest[], const ea::vector<VertexElement>& destElements, bool setMissingElementsToZero = true);

private:
    /// Update offsets of vertex elements.
    void UpdateOffsets();
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
    /// Number of vertices.
    unsigned vertexCount_{};
    /// Vertex size.
    unsigned vertexSize_{};
    /// Vertex elements.
    ea::vector<VertexElement> elements_;
    /// Vertex element hash.
    unsigned long long elementHash_{};
    /// Vertex element legacy bitmask.
    VertexMaskFlags elementMask_{};
    /// Buffer locking state.
    LockState lockState_{LOCK_NONE};
    /// Lock start vertex.
    unsigned lockStart_{};
    /// Lock number of vertices.
    unsigned lockCount_{};
    /// Scratch buffer for fallback locking.
    void* lockScratchData_{};
    /// Dynamic flag.
    bool dynamic_{};
    /// Shadowed flag.
    bool shadowed_{};
    /// Discard lock flag. Used by OpenGL only.
    bool discardLock_{};
};

/// Vertex Buffer of dynamic size. Resize policy is similar to standard vector.
class URHO3D_API DynamicVertexBuffer : public Object
{
    URHO3D_OBJECT(DynamicVertexBuffer, Object);

public:
    DynamicVertexBuffer(Context* context);
    bool Initialize(unsigned vertexCount, const ea::vector<VertexElement>& elements);

    /// Discard existing content of the buffer.
    void Discard();
    /// Commit all added data to GPU.
    void Commit();

    /// Allocate vertices. Returns index of first vertex and writeable buffer of sufficient size.
    ea::pair<unsigned, unsigned char*> AddVertices(unsigned count)
    {
        const unsigned startVertex = numVertices_;
        if (startVertex + count > maxNumVertices_)
            GrowBuffer();

        numVertices_ += count;
        unsigned char* data = shadowData_.data() + startVertex * vertexSize_;
        return { startVertex, data };
    }

    /// Store vertices. Returns index of first vertex.
    unsigned AddVertices(unsigned count, const void* data)
    {
        const auto indexAndData = AddVertices(count);
        memcpy(indexAndData.second, data, count * vertexSize_);
        return indexAndData.first;
    }

    VertexBuffer* GetVertexBuffer() const { return vertexBuffer_; }
    unsigned GetVertexCount() const { return numVertices_; }

private:
    void GrowBuffer();

    SharedPtr<VertexBuffer> vertexBuffer_;
    ByteVector shadowData_;
    bool vertexBufferNeedResize_{};

    unsigned vertexSize_{};
    unsigned numVertices_{};
    unsigned maxNumVertices_{};
};

}
