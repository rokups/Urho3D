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

#include <EASTL/sort.h>

#include <Urho3D/Core/WorkQueue.h>
#include <Urho3D/Core/Thread.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/IO/FileSystem.h>
#include <LZ4/lz4.h>
#include <LZ4/lz4hc.h>

#include "Project.h"
#include "Pipeline/Pipeline.h"
#include "Pipeline/Asset.h"
#include "Pipeline/Packager.h"


namespace Urho3D
{

Packager::Packager(Context* context)
    : Object(context)
    , output_(context)
{
    compressBuffer_.resize(LZ4_compressBound(blockSize_));
}

Packager::~Packager()
{
    assert(IsCompleted());
}

bool Packager::OpenPackage(const ea::string& path, Flavor* flavor, bool compress/*=true*/)
{
    assert(IsCompleted());
    outputPath_ = path;
    logger_ = Log::GetLogger(GetFileNameAndExtension(path));

    flavor_ = WeakPtr(flavor);
    compress_ = compress;

    if (output_.Open(path, FILE_WRITE))
    {
        WriteHeaders();
        return true;
    }
    logger_.Error("Opening '{}' failed, package was not created.", GetFileNameAndExtension(path));
    return false;
}

float Packager::GetProgress() const
{
    return (float)(1.0 / filesTotal_ * filesDone_);
}

bool Packager::IsCompleted() const
{
    return filesTotal_ == filesDone_;
}

void Packager::AddAsset(Asset* asset)
{
    assert(IsCompleted());
    queuedAssets_.push_back(SharedPtr(asset));
}

void Packager::Start()
{
    assert(IsCompleted());
    filesTotal_ = queuedAssets_.size() + 2;     // CacheInfo.json + Settings.json

    if (filesTotal_ == 0)
    {
        logger_.Warning("Resources directory is empty, package was not created.");
        output_.Close();
        context_->GetSubsystem<FileSystem>()->Delete(outputPath_);
        return;
    }

    context_->GetSubsystem<WorkQueue>()->AddWorkItem([this](unsigned /*threadIndex*/) { WritePackage(); });
}

void Packager::WritePackage()
{
    assert(!IsCompleted());
    logger_.Info("Packaging started.");

    ea::quick_sort(queuedAssets_.begin(), queuedAssets_.end(), [](const SharedPtr<Asset>& a, const SharedPtr<Asset>& b) {
        return a->GetName() < b->GetName();
    });

    auto* project = GetSubsystem<Project>();
    const ea::string& resourcePath = project->GetResourcePath();
    ea::string cachePath = flavor_->GetCachePath();

    for (Asset* asset : queuedAssets_)
    {
        // Asset may be importing at this time. We have to wait. Can not package another asset in this time because we want reproducible
        // packages.
        while (asset->IsImporting())
            Time::Sleep(1);

        bool writtenAny = false;
        for (AssetImporter* importer : asset->GetImporters(flavor_))
        {
            for (const ea::string& byproduct : importer->GetByproducts())   // Byproducts are sorted on import
            {
                AddFile(cachePath, byproduct);
                writtenAny = true;
            }
        }

        // Raw assets are only written to default flavor pak
        if (!writtenAny && flavor_->IsDefault())
            AddFile(resourcePath, asset->GetResourcePath());

        filesDone_++;
    }

    // Has to be done here in case any resources were imported during packaging.
    auto pipeline = GetSubsystem<Pipeline>();
    pipeline->CookSettings(); // TODO: Thread safety
    pipeline->CookCacheInfo();// TODO: Thread safety
    AddFile(cachePath, "CacheInfo.json");   filesDone_++;
    AddFile(cachePath, "Settings.json");    filesDone_++;

    entriesOffset_ = output_.GetSize();

    for (const FileEntry& entry : entries_)
    {
        output_.WriteString(entry.name_);
        output_.WriteUInt(entry.offset_);
        output_.WriteUInt(entry.size_);
        output_.WriteUInt(entry.checksum_);
    }
    // Write package size to the end of file to allow finding it linked to an executable file
    unsigned currentSize = output_.GetSize();
    output_.WriteUInt(currentSize + sizeof(unsigned));

    WriteHeaders();

    logger_.Info("Packaging completed.");
}

void Packager::WriteHeaders()
{
    output_.Seek(0);
    output_.WriteFileID(compress_ ? "RLZ4" : "RPAK");
    output_.WriteUInt(entries_.size());
    output_.WriteUInt(checksum_);
    output_.WriteUInt(0);                                   // Version. Reserved for future use.
    output_.WriteInt64(entriesOffset_);
}

bool Packager::AddFile(const ea::string& root, const ea::string& path)
{
    assert(root.ends_with("/"));

    FileEntry entry{};
    ea::string fileFullPath;

    if (IsAbsolutePath(path))
    {
        assert(root.starts_with(root));
        fileFullPath = path;
        entry.name_ = path.substr(root.length());
    }
    else
    {
        fileFullPath = root + path;
        entry.name_ = path;
    }
    entry.checksum_ = 0;
    entry.offset_ = 0;
    entry.size_ = File(context_, fileFullPath).GetSize();
    if (!entry.size_)
    {
        logger_.Warning("Skipped empty/missing file '{}'.", fileFullPath);
        return false;
    }

    unsigned lastOffset = entry.offset_ = output_.GetSize();

    File srcFile(context_, fileFullPath);
    if (!srcFile.IsOpen())
    {
        logger_.Error("Could not open file {}. Skipped!", fileFullPath);
        return false;
    }

    unsigned dataSize = entry.size_;
    buffer_.resize(dataSize);

    if (srcFile.Read(&buffer_[0], dataSize) != dataSize)
    {
        logger_.Error("Could not read file {}. Skipped!", fileFullPath);
        return false;
    }
    srcFile.Close();

    for (unsigned j = 0; j < dataSize; ++j)
    {
        checksum_ = SDBMHash(checksum_, buffer_[j]);
        entry.checksum_ = SDBMHash(entry.checksum_, buffer_[j]);
    }

    entries_.push_back(entry);
    if (!compress_)
    {
        logger_.Info("Added {} size {}", entry.name_, dataSize);
        output_.Write(&buffer_[0], entry.size_);
    }
    else
    {
        unsigned pos = 0;

        // TODO: This could be parallelized.
        while (pos < dataSize)
        {
            int unpackedSize = blockSize_;
            if (pos + unpackedSize > dataSize)
                unpackedSize = static_cast<int>(dataSize) - pos;

            auto packedSize = (unsigned) LZ4_compress_HC((const char*) &buffer_[pos], (char*) compressBuffer_.data(), unpackedSize,
                LZ4_compressBound(unpackedSize), 0);
            if (!packedSize)
                logger_.Error("LZ4 compression failed for file {} at offset {}.", entry.name_, pos);

            output_.WriteUShort((unsigned short) unpackedSize);
            output_.WriteUShort((unsigned short) packedSize);
            output_.Write(compressBuffer_.data(), packedSize);

            pos += unpackedSize;
        }

        unsigned totalPackedBytes = output_.GetSize() - lastOffset;
        logger_.Info("{} in: {} out: {} ratio: {}", entry.name_, dataSize, totalPackedBytes,
            totalPackedBytes ? 1.f * dataSize / totalPackedBytes : 0.f);
    }
    return true;
}

}
