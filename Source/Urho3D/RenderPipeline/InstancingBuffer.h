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

#include "../Core/Object.h"
#include "../Graphics/GraphicsDefs.h"
#include "../Graphics/VertexBuffer.h"
#include "../RenderPipeline/RenderPipelineDefs.h"

namespace Urho3D
{

/// Instancing buffer compositor.
class URHO3D_API InstancingBuffer : public Object
{
    URHO3D_OBJECT(InstancingBuffer, Object);

public:
    /// Stride of one element in bytes.
    static const unsigned ElementStride = 4 * sizeof(float);

    explicit InstancingBuffer(Context* context);
    void SetSettings(const InstancingBufferSettings& settings);

    /// Begin buffer composition.
    void Begin();
    /// End buffer composition and commit added instances to GPU.
    void End();

    /// Add "vertex" to buffer. Use SetElements to fill it after.
    unsigned AddInstance()
    {
        const unsigned currentVertex = nextVertex_;
        if (currentVertex >= numVertices_)
            GrowBuffer();

        ++nextVertex_;
        currentVertexData_ = data_.data() + currentVertex * vertexStride_;
        return currentVertex;
    }

    /// Set one or more 4-float elements in current instance.
    void SetElements(const void* data, unsigned index, unsigned count)
    {
        memcpy(currentVertexData_ + index * ElementStride, data, count * ElementStride);
    }

    /// Getters
    /// @{
    VertexBuffer* GetVertexBuffer() const { return vertexBuffer_; }
    bool IsEnabled() const { return settings_.enableInstancing_; }
    /// @}

private:
    /// Initialize instancing buffer.
    void Initialize();
    /// Grow buffer.
    void GrowBuffer();

    /// Settings.
    InstancingBufferSettings settings_;

    /// GPU buffer.
    SharedPtr<VertexBuffer> vertexBuffer_;
    /// Whether the GPU buffer has to be resized.
    bool vertexBufferDirty_{};
    /// Buffer layout.
    ea::vector<VertexElement> vertexElements_;
    /// Vertex stride.
    unsigned vertexStride_{};

    /// Number of vertices in buffer.
    unsigned numVertices_{};
    /// Buffer data.
    ByteVector data_;

    /// Index of next vertex.
    unsigned nextVertex_{};
    /// Pointer to current vertex data.
    unsigned char* currentVertexData_{};
};

}
