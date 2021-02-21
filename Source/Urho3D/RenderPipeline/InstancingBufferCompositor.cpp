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

#include "../IO/Log.h"
#include "../RenderPipeline/InstancingBufferCompositor.h"

#include "../DebugNew.h"

namespace Urho3D
{

InstancingBufferCompositor::InstancingBufferCompositor(Context* context)
    : Object(context)
{
}

void InstancingBufferCompositor::SetSettings(const InstancingBufferCompositorSettings& settings)
{
    if (settings_ != settings)
    {
        settings_ = settings;
        Initialize();
    }
}

void InstancingBufferCompositor::Begin()
{
    nextVertex_ = 0;
}

void InstancingBufferCompositor::End()
{
    if (nextVertex_ == 0 || !settings_.enable_)
        return;

    if (vertexBufferDirty_)
    {
        vertexBufferDirty_ = false;
        if (!vertexBuffer_->SetSize(numVertices_, vertexElements_, true))
        {
            URHO3D_LOGERROR("Failed to create instancing buffer of {} vertices with stride {}", numVertices_, vertexStride_);
            return;
        }
    }

    vertexBuffer_->SetData(data_.data());
}

void InstancingBufferCompositor::Initialize()
{
    nextVertex_ = 0;
    vertexElements_.clear();
    data_.clear();
    vertexBuffer_ = nullptr;

    if (settings_.enable_)
    {
        for (unsigned i = 0; i < settings_.numReservedElems_; ++i)
        {
            const unsigned index = settings_.firstUnusedTexCoord_ + i;
            vertexElements_.push_back(VertexElement(TYPE_VECTOR4, SEM_TEXCOORD, index, true));
        }
        vertexStride_ = settings_.numReservedElems_ * ElementStride;
        vertexBufferDirty_ = true;

        vertexBuffer_ = MakeShared<VertexBuffer>(context_);
    }
}

void InstancingBufferCompositor::GrowBuffer()
{
    numVertices_ = numVertices_ > 0 ? 2 * numVertices_ : 128;
    data_.resize(numVertices_ * vertexStride_);
    vertexBufferDirty_ = true;
}

}
