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

// This file contains IndexBuffer code common to all graphics APIs.

#include "../Precompiled.h"

#include "../Core/Context.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/GraphicsEvents.h"
#include "../Graphics/IndexBuffer.h"
#include "../IO/Log.h"

#include "../DebugNew.h"

namespace Urho3D
{

IndexBuffer::IndexBuffer(Context* context, bool forceHeadless) :
    Object(context),
    GPUObject(forceHeadless ? nullptr : GetSubsystem<Graphics>()),
    indexCount_(0),
    indexSize_(0),
    lockState_(LOCK_NONE),
    lockStart_(0),
    lockCount_(0),
    lockScratchData_(nullptr),
    shadowed_(false),
    dynamic_(false),
    discardLock_(false)
{
    // Force shadowing mode if graphics subsystem does not exist
    if (!graphics_)
        shadowed_ = true;
}

IndexBuffer::~IndexBuffer()
{
    Release();
}

extern const char* GEOMETRY_CATEGORY;

void IndexBuffer::RegisterObject(Context* context)
{
    context->RegisterFactory<IndexBuffer>();
}

void IndexBuffer::SetShadowed(bool enable)
{
    // If no graphics subsystem, can not disable shadowing
    if (!graphics_)
        enable = true;

    if (enable != shadowed_)
    {
        if (enable && indexCount_ && indexSize_)
            shadowData_ = new unsigned char[indexCount_ * indexSize_];
        else
            shadowData_.reset();

        shadowed_ = enable;
    }
}

bool IndexBuffer::SetSize(unsigned indexCount, bool largeIndices, bool dynamic)
{
    Unlock();

    indexCount_ = indexCount;
    indexSize_ = (unsigned)(largeIndices ? sizeof(unsigned) : sizeof(unsigned short));
    dynamic_ = dynamic;

    SendEvent(E_BUFFERFORMATCHANGED);

    if (shadowed_ && indexCount_ && indexSize_)
        shadowData_ = new unsigned char[indexCount_ * indexSize_];
    else
        shadowData_.reset();

    return Create();
}

bool IndexBuffer::GetUsedVertexRange(unsigned start, unsigned count, unsigned& minVertex, unsigned& vertexCount)
{
    if (!shadowData_)
    {
        URHO3D_LOGERROR("Used vertex range can only be queried from an index buffer with shadow data");
        return false;
    }

    if (start + count > indexCount_)
    {
        URHO3D_LOGERROR("Illegal index range for querying used vertices");
        return false;
    }

    minVertex = M_MAX_UNSIGNED;
    unsigned maxVertex = 0;

    if (indexSize_ == sizeof(unsigned))
    {
        unsigned* indices = ((unsigned*)shadowData_.get()) + start;

        for (unsigned i = 0; i < count; ++i)
        {
            if (indices[i] < minVertex)
                minVertex = indices[i];
            if (indices[i] > maxVertex)
                maxVertex = indices[i];
        }
    }
    else
    {
        unsigned short* indices = ((unsigned short*)shadowData_.get()) + start;

        for (unsigned i = 0; i < count; ++i)
        {
            if (indices[i] < minVertex)
                minVertex = indices[i];
            if (indices[i] > maxVertex)
                maxVertex = indices[i];
        }
    }

    vertexCount = maxVertex - minVertex + 1;
    return true;
}

ea::vector<unsigned> IndexBuffer::GetUnpackedData(unsigned start, unsigned count) const
{
    if (start >= indexCount_ || count == 0 || !IsShadowed())
        return {};

    // Clamp count to index buffer size.
    if (count == M_MAX_UNSIGNED || start + count > indexCount_)
        count = indexCount_ - start;

    // Unpack data
    const bool largeIndices = indexSize_ == 4;
    ea::vector<unsigned> result(count);
    UnpackIndexData(GetShadowData(), largeIndices, start, count, result.data());
    return result;
}

void IndexBuffer::SetUnpackedData(const unsigned data[], unsigned start, unsigned count)
{
    if (start >= indexCount_ || count == 0)
        return;

    // Clamp count to index buffer size.
    if (count == M_MAX_UNSIGNED || start + count > indexCount_)
        count = indexCount_ - start;

    const bool largeIndices = indexSize_ == 4;
    ea::vector<unsigned char> buffer(count * indexSize_);

    PackIndexData(data, buffer.data(), largeIndices, 0, count);
    SetDataRange(buffer.data(), start, count);
}

void IndexBuffer::UnpackIndexData(const void* source, bool largeIndices, unsigned start, unsigned count, unsigned dest[])
{
    const unsigned stride = largeIndices ? 4 : 2;
    const unsigned char* sourceBytes = reinterpret_cast<const unsigned char*>(source) + start * stride;

    if (largeIndices)
    {
        memcpy(dest, sourceBytes, count * stride);
    }
    else
    {
        for (unsigned i = 0; i < count; ++i)
        {
            // May by unaligned
            unsigned short index{};
            memcpy(&index, &sourceBytes[i * stride], sizeof(index));
            dest[i] = index;
        }
    }
}

void IndexBuffer::PackIndexData(const unsigned source[], void* dest, bool largeIndices, unsigned start, unsigned count)
{
    const unsigned stride = largeIndices ? 4 : 2;
    unsigned char* destBytes = reinterpret_cast<unsigned char*>(dest) + start * stride;

    if (largeIndices)
    {
        memcpy(destBytes, source, count * stride);
    }
    else
    {
        for (unsigned i = 0; i < count; ++i)
        {
            const unsigned short index = static_cast<unsigned short>(source[i]);
            memcpy(&destBytes[i * stride], &index, sizeof(index));
        }
    }
}

}
