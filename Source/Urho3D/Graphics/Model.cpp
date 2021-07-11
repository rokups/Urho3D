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

#include "../Precompiled.h"

#include "../Core/Context.h"
#include "../Core/Profiler.h"
#include "../Graphics/Geometry.h"
#include "../Graphics/IndexBuffer.h"
#include "../Graphics/Model.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/VertexBuffer.h"
#include "../IO/Log.h"
#include "../IO/File.h"
#include "../IO/FileSystem.h"
#include "../Resource/ResourceCache.h"
#include "../Resource/XMLFile.h"

#include "../DebugNew.h"

namespace Urho3D
{

unsigned LookupVertexBuffer(VertexBuffer* buffer, const ea::vector<SharedPtr<VertexBuffer> >& buffers)
{
    for (unsigned i = 0; i < buffers.size(); ++i)
    {
        if (buffers[i] == buffer)
            return i;
    }
    return 0;
}

unsigned LookupIndexBuffer(IndexBuffer* buffer, const ea::vector<SharedPtr<IndexBuffer> >& buffers)
{
    for (unsigned i = 0; i < buffers.size(); ++i)
    {
        if (buffers[i] == buffer)
            return i;
    }
    return 0;
}

Model::Model(Context* context) :
    ResourceWithMetadata(context)
{
}

Model::~Model() = default;

void Model::RegisterObject(Context* context)
{
    context->RegisterFactory<Model>();
}

bool Model::BeginLoad(Deserializer& source)
{
    // Check ID
    ea::string fileID = source.ReadFileID();
    if (fileID != "UMDL" && fileID != "UMD2")
    {
        URHO3D_LOGERROR(source.GetName() + " is not a valid model file");
        return false;
    }

    bool hasVertexDeclarations = (fileID == "UMD2");

    geometries_.clear();
    geometryBoneMappings_.clear();
    geometryCenters_.clear();
    morphs_.clear();
    vertexBuffers_.clear();
    indexBuffers_.clear();

    unsigned memoryUse = sizeof(Model);
    bool async = GetAsyncLoadState() == ASYNC_LOADING;

    // Read vertex buffers
    unsigned numVertexBuffers = source.ReadUInt();
    vertexBuffers_.reserve(numVertexBuffers);
    morphRangeStarts_.resize(numVertexBuffers);
    morphRangeCounts_.resize(numVertexBuffers);
    loadVBData_.resize(numVertexBuffers);
    for (unsigned i = 0; i < numVertexBuffers; ++i)
    {
        VertexBufferDesc& desc = loadVBData_[i];

        desc.vertexCount_ = source.ReadUInt();
        if (!hasVertexDeclarations)
        {
            unsigned elementMask = source.ReadUInt();
            desc.vertexElements_ = VertexBuffer::GetElements(elementMask);
        }
        else
        {
            desc.vertexElements_.clear();
            unsigned numElements = source.ReadUInt();
            for (unsigned j = 0; j < numElements; ++j)
            {
                unsigned elementDesc = source.ReadUInt();
                auto type = (VertexElementType)(elementDesc & 0xffu);
                auto semantic = (VertexElementSemantic)((elementDesc >> 8u) & 0xffu);
                auto index = (unsigned char)((elementDesc >> 16u) & 0xffu);
                desc.vertexElements_.push_back(VertexElement(type, semantic, index));
            }
        }

        morphRangeStarts_[i] = source.ReadUInt();
        morphRangeCounts_[i] = source.ReadUInt();

        SharedPtr<VertexBuffer> buffer(context_->CreateObject<VertexBuffer>());
        unsigned vertexSize = VertexBuffer::GetVertexSize(desc.vertexElements_);
        desc.dataSize_ = desc.vertexCount_ * vertexSize;

        // Prepare vertex buffer data to be uploaded during EndLoad()
        if (async)
        {
            desc.data_ = new unsigned char[desc.dataSize_];
            source.Read(desc.data_.get(), desc.dataSize_);
        }
        else
        {
            // If not async loading, use locking to avoid extra allocation & copy
            desc.data_.reset(); // Make sure no previous data
            buffer->SetShadowed(true);
            buffer->SetSize(desc.vertexCount_, desc.vertexElements_);
            void* dest = buffer->Lock(0, desc.vertexCount_);
            source.Read(dest, desc.vertexCount_ * vertexSize);
            buffer->Unlock();
        }

        memoryUse += sizeof(VertexBuffer) + desc.vertexCount_ * vertexSize;
        vertexBuffers_.push_back(buffer);
    }

    // Read index buffers
    unsigned numIndexBuffers = source.ReadUInt();
    indexBuffers_.reserve(numIndexBuffers);
    loadIBData_.resize(numIndexBuffers);
    for (unsigned i = 0; i < numIndexBuffers; ++i)
    {
        unsigned indexCount = source.ReadUInt();
        unsigned indexSize = source.ReadUInt();

        SharedPtr<IndexBuffer> buffer(context_->CreateObject<IndexBuffer>());

        // Prepare index buffer data to be uploaded during EndLoad()
        if (async)
        {
            loadIBData_[i].indexCount_ = indexCount;
            loadIBData_[i].indexSize_ = indexSize;
            loadIBData_[i].dataSize_ = indexCount * indexSize;
            loadIBData_[i].data_ = new unsigned char[loadIBData_[i].dataSize_];
            source.Read(loadIBData_[i].data_.get(), loadIBData_[i].dataSize_);
        }
        else
        {
            // If not async loading, use locking to avoid extra allocation & copy
            loadIBData_[i].data_.reset(); // Make sure no previous data
            buffer->SetShadowed(true);
            buffer->SetSize(indexCount, indexSize > sizeof(unsigned short));
            void* dest = buffer->Lock(0, indexCount);
            source.Read(dest, indexCount * indexSize);
            buffer->Unlock();
        }

        memoryUse += sizeof(IndexBuffer) + indexCount * indexSize;
        indexBuffers_.push_back(buffer);
    }

    // Read geometries
    unsigned numGeometries = source.ReadUInt();
    geometries_.reserve(numGeometries);
    geometryBoneMappings_.reserve(numGeometries);
    geometryCenters_.reserve(numGeometries);
    loadGeometries_.resize(numGeometries);
    for (unsigned i = 0; i < numGeometries; ++i)
    {
        // Read bone mappings
        unsigned boneMappingCount = source.ReadUInt();
        ea::vector<unsigned> boneMapping(boneMappingCount);
        for (unsigned j = 0; j < boneMappingCount; ++j)
            boneMapping[j] = source.ReadUInt();
        geometryBoneMappings_.push_back(boneMapping);

        unsigned numLodLevels = source.ReadUInt();
        ea::vector<SharedPtr<Geometry> > geometryLodLevels;
        geometryLodLevels.reserve(numLodLevels);
        loadGeometries_[i].resize(numLodLevels);

        for (unsigned j = 0; j < numLodLevels; ++j)
        {
            float distance = source.ReadFloat();
            auto type = (PrimitiveType)source.ReadUInt();

            unsigned vbRef = source.ReadUInt();
            unsigned ibRef = source.ReadUInt();
            unsigned indexStart = source.ReadUInt();
            unsigned indexCount = source.ReadUInt();

            if (vbRef >= vertexBuffers_.size())
            {
                URHO3D_LOGERROR("Vertex buffer index out of bounds");
                loadVBData_.clear();
                loadIBData_.clear();
                loadGeometries_.clear();
                return false;
            }
            if (ibRef >= indexBuffers_.size())
            {
                URHO3D_LOGERROR("Index buffer index out of bounds");
                loadVBData_.clear();
                loadIBData_.clear();
                loadGeometries_.clear();
                return false;
            }

            SharedPtr<Geometry> geometry(context_->CreateObject<Geometry>());
            geometry->SetLodDistance(distance);

            // Prepare geometry to be defined during EndLoad()
            loadGeometries_[i][j].type_ = type;
            loadGeometries_[i][j].vbRef_ = vbRef;
            loadGeometries_[i][j].ibRef_ = ibRef;
            loadGeometries_[i][j].indexStart_ = indexStart;
            loadGeometries_[i][j].indexCount_ = indexCount;

            geometryLodLevels.push_back(geometry);
            memoryUse += sizeof(Geometry);
        }

        geometries_.push_back(geometryLodLevels);
    }

    // Read morphs
    unsigned numMorphs = source.ReadUInt();
    morphs_.reserve(numMorphs);
    for (unsigned i = 0; i < numMorphs; ++i)
    {
        ModelMorph newMorph;

        newMorph.name_ = source.ReadString();
        newMorph.nameHash_ = newMorph.name_;
        newMorph.weight_ = 0.0f;
        unsigned numBuffers = source.ReadUInt();

        for (unsigned j = 0; j < numBuffers; ++j)
        {
            VertexBufferMorph newBuffer;
            unsigned bufferIndex = source.ReadUInt();

            newBuffer.elementMask_ = VertexMaskFlags(source.ReadUInt());
            newBuffer.vertexCount_ = source.ReadUInt();

            // Base size: size of each vertex index
            unsigned vertexSize = sizeof(unsigned);
            // Add size of individual elements
            if (newBuffer.elementMask_ & MASK_POSITION)
                vertexSize += sizeof(Vector3);
            if (newBuffer.elementMask_ & MASK_NORMAL)
                vertexSize += sizeof(Vector3);
            if (newBuffer.elementMask_ & MASK_TANGENT)
                vertexSize += sizeof(Vector3);
            newBuffer.dataSize_ = newBuffer.vertexCount_ * vertexSize;
            newBuffer.morphData_ = new unsigned char[newBuffer.dataSize_];

            source.Read(&newBuffer.morphData_[0], newBuffer.vertexCount_ * vertexSize);

            newMorph.buffers_[bufferIndex] = newBuffer;
            memoryUse += sizeof(VertexBufferMorph) + newBuffer.vertexCount_ * vertexSize;
        }

        morphs_.push_back(newMorph);
        memoryUse += sizeof(ModelMorph);
    }

    // Read skeleton
    skeleton_.Load(source);
    memoryUse += skeleton_.GetNumBones() * sizeof(Bone);

    // Read bounding box
    boundingBox_ = source.ReadBoundingBox();

    // Read geometry centers
    for (unsigned i = 0; i < geometries_.size() && !source.IsEof(); ++i)
        geometryCenters_.push_back(source.ReadVector3());
    while (geometryCenters_.size() < geometries_.size())
        geometryCenters_.push_back(Vector3::ZERO);
    memoryUse += sizeof(Vector3) * geometries_.size();

    // Read metadata
    auto* cache = GetSubsystem<ResourceCache>();
    ea::string xmlName = ReplaceExtension(GetName(), ".xml");
    SharedPtr<XMLFile> file(cache->GetTempResource<XMLFile>(xmlName, false));
    if (file)
        LoadMetadataFromXML(file->GetRoot());

    SetMemoryUse(memoryUse);
    return true;
}

bool Model::EndLoad()
{
    // Upload vertex buffer data
    for (unsigned i = 0; i < vertexBuffers_.size(); ++i)
    {
        VertexBuffer* buffer = vertexBuffers_[i];
        VertexBufferDesc& desc = loadVBData_[i];
        if (desc.data_)
        {
            buffer->SetShadowed(true);
            buffer->SetSize(desc.vertexCount_, desc.vertexElements_);
            buffer->SetData(desc.data_.get());
        }
    }

    // Upload index buffer data
    for (unsigned i = 0; i < indexBuffers_.size(); ++i)
    {
        IndexBuffer* buffer = indexBuffers_[i];
        IndexBufferDesc& desc = loadIBData_[i];
        if (desc.data_)
        {
            buffer->SetShadowed(true);
            buffer->SetSize(desc.indexCount_, desc.indexSize_ > sizeof(unsigned short));
            buffer->SetData(desc.data_.get());
        }
    }

    // Set up geometries
    for (unsigned i = 0; i < geometries_.size(); ++i)
    {
        for (unsigned j = 0; j < geometries_[i].size(); ++j)
        {
            Geometry* geometry = geometries_[i][j];
            GeometryDesc& desc = loadGeometries_[i][j];
            geometry->SetVertexBuffer(0, vertexBuffers_[desc.vbRef_]);
            geometry->SetIndexBuffer(indexBuffers_[desc.ibRef_]);
            geometry->SetDrawRange(desc.type_, desc.indexStart_, desc.indexCount_);
        }
    }

    loadVBData_.clear();
    loadIBData_.clear();
    loadGeometries_.clear();
    return true;
}

bool Model::Save(Serializer& dest) const
{
    // Write ID
    if (!dest.WriteFileID("UMD2"))
        return false;

    // Write vertex buffers
    dest.WriteUInt(vertexBuffers_.size());
    for (unsigned i = 0; i < vertexBuffers_.size(); ++i)
    {
        VertexBuffer* buffer = vertexBuffers_[i];
        dest.WriteUInt(buffer->GetVertexCount());
        const ea::vector<VertexElement>& elements = buffer->GetElements();
        dest.WriteUInt(elements.size());
        for (unsigned j = 0; j < elements.size(); ++j)
        {
            unsigned elementDesc = ((unsigned)elements[j].type_) |
                (((unsigned)elements[j].semantic_) << 8u) |
                (((unsigned)elements[j].index_) << 16u);
            dest.WriteUInt(elementDesc);
        }
        dest.WriteUInt(morphRangeStarts_[i]);
        dest.WriteUInt(morphRangeCounts_[i]);
        dest.Write(buffer->GetShadowData(), buffer->GetVertexCount() * buffer->GetVertexSize());
    }
    // Write index buffers
    dest.WriteUInt(indexBuffers_.size());
    for (unsigned i = 0; i < indexBuffers_.size(); ++i)
    {
        IndexBuffer* buffer = indexBuffers_[i];
        dest.WriteUInt(buffer->GetIndexCount());
        dest.WriteUInt(buffer->GetIndexSize());
        dest.Write(buffer->GetShadowData(), buffer->GetIndexCount() * buffer->GetIndexSize());
    }
    // Write geometries
    dest.WriteUInt(geometries_.size());
    for (unsigned i = 0; i < geometries_.size(); ++i)
    {
        // Write bone mappings
        dest.WriteUInt(geometryBoneMappings_[i].size());
        for (unsigned j = 0; j < geometryBoneMappings_[i].size(); ++j)
            dest.WriteUInt(geometryBoneMappings_[i][j]);

        // Write the LOD levels
        dest.WriteUInt(geometries_[i].size());
        for (unsigned j = 0; j < geometries_[i].size(); ++j)
        {
            Geometry* geometry = geometries_[i][j];
            dest.WriteFloat(geometry->GetLodDistance());
            dest.WriteUInt(geometry->GetPrimitiveType());
            dest.WriteUInt(LookupVertexBuffer(geometry->GetVertexBuffer(0), vertexBuffers_));
            dest.WriteUInt(LookupIndexBuffer(geometry->GetIndexBuffer(), indexBuffers_));
            dest.WriteUInt(geometry->GetIndexStart());
            dest.WriteUInt(geometry->GetIndexCount());
        }
    }

    // Write morphs
    dest.WriteUInt(morphs_.size());
    for (unsigned i = 0; i < morphs_.size(); ++i)
    {
        dest.WriteString(morphs_[i].name_);
        dest.WriteUInt(morphs_[i].buffers_.size());

        // Write morph vertex buffers
        for (auto j = morphs_[i].buffers_.begin();
             j != morphs_[i].buffers_.end(); ++j)
        {
            dest.WriteUInt(j->first);
            dest.WriteUInt(j->second.elementMask_);
            dest.WriteUInt(j->second.vertexCount_);

            // Base size: size of each vertex index
            unsigned vertexSize = sizeof(unsigned);
            // Add size of individual elements
            if (j->second.elementMask_ & MASK_POSITION)
                vertexSize += sizeof(Vector3);
            if (j->second.elementMask_ & MASK_NORMAL)
                vertexSize += sizeof(Vector3);
            if (j->second.elementMask_ & MASK_TANGENT)
                vertexSize += sizeof(Vector3);

            dest.Write(j->second.morphData_.get(), vertexSize * j->second.vertexCount_);
        }
    }

    // Write skeleton
    skeleton_.Save(dest);

    // Write bounding box
    dest.WriteBoundingBox(boundingBox_);

    // Write geometry centers
    for (unsigned i = 0; i < geometryCenters_.size(); ++i)
        dest.WriteVector3(geometryCenters_[i]);

    // Write metadata
    if (HasMetadata())
    {
        auto* destFile = dynamic_cast<File*>(&dest);
        if (destFile)
        {
            ea::string xmlName = ReplaceExtension(destFile->GetName(), ".xml");

            SharedPtr<XMLFile> xml(context_->CreateObject<XMLFile>());
            XMLElement rootElem = xml->CreateRoot("model");
            SaveMetadataToXML(rootElem);

            File xmlFile(context_, xmlName, FILE_WRITE);
            xml->Save(xmlFile);
        }
        else
            URHO3D_LOGWARNING("Can not save model metadata when not saving into a file");
    }

    return true;
}

void Model::SetBoundingBox(const BoundingBox& box)
{
    boundingBox_ = box;
}

bool Model::SetVertexBuffers(const ea::vector<SharedPtr<VertexBuffer> >& buffers, const ea::vector<unsigned>& morphRangeStarts,
    const ea::vector<unsigned>& morphRangeCounts)
{
    for (unsigned i = 0; i < buffers.size(); ++i)
    {
        if (!buffers[i])
        {
            URHO3D_LOGERROR("Null model vertex buffers specified");
            return false;
        }
        if (!buffers[i]->IsShadowed())
        {
            URHO3D_LOGERROR("Model vertex buffers must be shadowed");
            return false;
        }
    }

    vertexBuffers_ = buffers;
    morphRangeStarts_.resize(buffers.size());
    morphRangeCounts_.resize(buffers.size());

    // If morph ranges are not specified for buffers, assume to be zero
    for (unsigned i = 0; i < buffers.size(); ++i)
    {
        morphRangeStarts_[i] = i < morphRangeStarts.size() ? morphRangeStarts[i] : 0;
        morphRangeCounts_[i] = i < morphRangeCounts.size() ? morphRangeCounts[i] : 0;
    }

    return true;
}

bool Model::SetIndexBuffers(const ea::vector<SharedPtr<IndexBuffer> >& buffers)
{
    for (unsigned i = 0; i < buffers.size(); ++i)
    {
        if (!buffers[i])
        {
            URHO3D_LOGERROR("Null model index buffers specified");
            return false;
        }
        if (!buffers[i]->IsShadowed())
        {
            URHO3D_LOGERROR("Model index buffers must be shadowed");
            return false;
        }
    }

    indexBuffers_ = buffers;
    return true;
}

void Model::SetNumGeometries(unsigned num)
{
    geometries_.resize(num);
    geometryBoneMappings_.resize(num);
    geometryCenters_.resize(num);

    // For easier creation of from-scratch geometry, ensure that all geometries start with at least 1 LOD level (0 makes no sense)
    for (unsigned i = 0; i < geometries_.size(); ++i)
    {
        if (geometries_[i].empty())
            geometries_[i].resize(1);
    }
}

bool Model::SetNumGeometryLodLevels(unsigned index, unsigned num)
{
    if (index >= geometries_.size())
    {
        URHO3D_LOGERROR("Geometry index out of bounds");
        return false;
    }
    if (!num)
    {
        URHO3D_LOGERROR("Zero LOD levels not allowed");
        return false;
    }

    geometries_[index].resize(num);
    return true;
}

bool Model::SetGeometry(unsigned index, unsigned lodLevel, Geometry* geometry)
{
    if (index >= geometries_.size())
    {
        URHO3D_LOGERROR("Geometry index out of bounds");
        return false;
    }
    if (lodLevel >= geometries_[index].size())
    {
        URHO3D_LOGERROR("LOD level index out of bounds");
        return false;
    }

    geometries_[index][lodLevel] = geometry;
    return true;
}

bool Model::SetGeometryCenter(unsigned index, const Vector3& center)
{
    if (index >= geometryCenters_.size())
    {
        URHO3D_LOGERROR("Geometry index out of bounds");
        return false;
    }

    geometryCenters_[index] = center;
    return true;
}

void Model::SetSkeleton(const Skeleton& skeleton)
{
    skeleton_ = skeleton;
}

void Model::SetGeometryBoneMappings(const ea::vector<ea::vector<unsigned> >& geometryBoneMappings)
{
    geometryBoneMappings_ = geometryBoneMappings;
}

void Model::SetMorphs(const ea::vector<ModelMorph>& morphs)
{
    morphs_ = morphs;
}

SharedPtr<Model> Model::Clone(const ea::string& cloneName) const
{
    SharedPtr<Model> ret(context_->CreateObject<Model>());

    ret->SetName(cloneName);
    ret->boundingBox_ = boundingBox_;
    ret->skeleton_ = skeleton_;
    ret->geometryBoneMappings_ = geometryBoneMappings_;
    ret->geometryCenters_ = geometryCenters_;
    ret->morphs_ = morphs_;
    ret->morphRangeStarts_ = morphRangeStarts_;
    ret->morphRangeCounts_ = morphRangeCounts_;

    // Deep copy vertex/index buffers
    ea::unordered_map<VertexBuffer*, VertexBuffer*> vbMapping;
    for (auto i = vertexBuffers_.begin(); i != vertexBuffers_.end(); ++i)
    {
        VertexBuffer* origBuffer = *i;
        SharedPtr<VertexBuffer> cloneBuffer;

        if (origBuffer)
        {
            cloneBuffer = context_->CreateObject<VertexBuffer>();
            cloneBuffer->SetSize(origBuffer->GetVertexCount(), origBuffer->GetElements(), origBuffer->IsDynamic());
            cloneBuffer->SetShadowed(origBuffer->IsShadowed());
            if (origBuffer->IsShadowed())
                cloneBuffer->SetData(origBuffer->GetShadowData());
            else
            {
                void* origData = origBuffer->Lock(0, origBuffer->GetVertexCount());
                if (origData)
                    cloneBuffer->SetData(origData);
                else
                    URHO3D_LOGERROR("Failed to lock original vertex buffer for copying");
            }
            vbMapping[origBuffer] = cloneBuffer;
        }

        ret->vertexBuffers_.push_back(cloneBuffer);
    }

    ea::unordered_map<IndexBuffer*, IndexBuffer*> ibMapping;
    for (auto i = indexBuffers_.begin(); i != indexBuffers_.end(); ++i)
    {
        IndexBuffer* origBuffer = *i;
        SharedPtr<IndexBuffer> cloneBuffer;

        if (origBuffer)
        {
            cloneBuffer = context_->CreateObject<IndexBuffer>();
            cloneBuffer->SetSize(origBuffer->GetIndexCount(), origBuffer->GetIndexSize() == sizeof(unsigned),
                origBuffer->IsDynamic());
            cloneBuffer->SetShadowed(origBuffer->IsShadowed());
            if (origBuffer->IsShadowed())
                cloneBuffer->SetData(origBuffer->GetShadowData());
            else
            {
                void* origData = origBuffer->Lock(0, origBuffer->GetIndexCount());
                if (origData)
                    cloneBuffer->SetData(origData);
                else
                    URHO3D_LOGERROR("Failed to lock original index buffer for copying");
            }
            ibMapping[origBuffer] = cloneBuffer;
        }

        ret->indexBuffers_.push_back(cloneBuffer);
    }

    // Deep copy all the geometry LOD levels and refer to the copied vertex/index buffers
    ret->geometries_.resize(geometries_.size());
    for (unsigned i = 0; i < geometries_.size(); ++i)
    {
        ret->geometries_[i].resize(geometries_[i].size());
        for (unsigned j = 0; j < geometries_[i].size(); ++j)
        {
            SharedPtr<Geometry> cloneGeometry;
            Geometry* origGeometry = geometries_[i][j];

            if (origGeometry)
            {
                cloneGeometry = context_->CreateObject<Geometry>();
                cloneGeometry->SetIndexBuffer(ibMapping[origGeometry->GetIndexBuffer()]);
                unsigned numVbs = origGeometry->GetNumVertexBuffers();
                for (unsigned k = 0; k < numVbs; ++k)
                {
                    cloneGeometry->SetVertexBuffer(k, vbMapping[origGeometry->GetVertexBuffer(k)]);
                }
                cloneGeometry->SetDrawRange(origGeometry->GetPrimitiveType(), origGeometry->GetIndexStart(),
                    origGeometry->GetIndexCount(), origGeometry->GetVertexStart(), origGeometry->GetVertexCount(), false);
                cloneGeometry->SetLodDistance(origGeometry->GetLodDistance());
            }

            ret->geometries_[i][j] = cloneGeometry;
        }
    }


    // Deep copy the morph data (if any) to allow modifying it
    for (auto i = ret->morphs_.begin(); i != ret->morphs_.end(); ++i)
    {
        ModelMorph& morph = *i;
        for (auto j = morph.buffers_.begin(); j !=
            morph.buffers_.end(); ++j)
        {
            VertexBufferMorph& vbMorph = j->second;
            if (vbMorph.dataSize_)
            {
                ea::shared_array<unsigned char> cloneData(new unsigned char[vbMorph.dataSize_]);
                memcpy(cloneData.get(), vbMorph.morphData_.get(), vbMorph.dataSize_);
                vbMorph.morphData_ = cloneData;
            }
        }
    }

    ret->SetMemoryUse(GetMemoryUse());

    return ret;
}

unsigned Model::GetNumGeometryLodLevels(unsigned index) const
{
    return index < geometries_.size() ? geometries_[index].size() : 0;
}

Geometry* Model::GetGeometry(unsigned index, unsigned lodLevel) const
{
    if (index >= geometries_.size() || geometries_[index].empty())
        return nullptr;

    if (lodLevel >= geometries_[index].size())
        lodLevel = geometries_[index].size() - 1;

    return geometries_[index][lodLevel];
}

const ModelMorph* Model::GetMorph(unsigned index) const
{
    return index < morphs_.size() ? &morphs_[index] : nullptr;
}

const ModelMorph* Model::GetMorph(const ea::string& name) const
{
    return GetMorph(StringHash(name));
}

const ModelMorph* Model::GetMorph(StringHash nameHash) const
{
    for (auto i = morphs_.begin(); i != morphs_.end(); ++i)
    {
        if (i->nameHash_ == nameHash)
            return &(*i);
    }

    return nullptr;
}

unsigned Model::GetMorphRangeStart(unsigned bufferIndex) const
{
    return bufferIndex < vertexBuffers_.size() ? morphRangeStarts_[bufferIndex] : 0;
}

unsigned Model::GetMorphRangeCount(unsigned bufferIndex) const
{
    return bufferIndex < vertexBuffers_.size() ? morphRangeCounts_[bufferIndex] : 0;
}

}
