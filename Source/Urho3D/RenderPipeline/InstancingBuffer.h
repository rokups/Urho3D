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

    /// Return index of next added instance.
    unsigned GetNextInstanceIndex() const { return vertexBuffer_->GetVertexCount(); }

    /// Add instance to buffer. Use SetElements to fill it after.
    unsigned AddInstance()
    {
        const auto indexAndData = vertexBuffer_->AddVertices(1);
        currentInstanceData_ = indexAndData.second;
        return indexAndData.first;
    }

    /// Set one or more 4-float elements in current instance.
    void SetElements(const void* data, unsigned index, unsigned count)
    {
        memcpy(currentInstanceData_ + index * ElementStride, data, count * ElementStride);
    }

    /// Getters
    /// @{
    const InstancingBufferSettings& GetSettings() const { return settings_; }
    VertexBuffer* GetVertexBuffer() const { return vertexBuffer_->GetVertexBuffer(); }
    bool IsEnabled() const { return settings_.enableInstancing_; }
    /// @}

private:
    void Initialize();

    InstancingBufferSettings settings_;
    SharedPtr<DynamicVertexBuffer> vertexBuffer_;

    unsigned char* currentInstanceData_{};
};

}
