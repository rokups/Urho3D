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
#include "../Core/CoreEvents.h"
#include "../Core/Profiler.h"
#include "../Core/WorkQueue.h"
#include "../IO/FileSystem.h"
#include "../IO/FileWatcher.h"
#include "../IO/Log.h"
#include "../IO/PackageFile.h"
#include "../Resource/BackgroundLoader.h"
#include "../Resource/BinaryFile.h"
#include "../Resource/Image.h"
#include "../Resource/ImageCube.h"
#include "../Resource/JSONFile.h"
#include "../Resource/PListFile.h"
#include "../Resource/ResourceCache.h"
#include "../Resource/ResourceEvents.h"
#include "../Resource/XMLFile.h"

#include "../DebugNew.h"

#include <cstdio>

namespace Urho3D
{

bool NeedToReloadDependencies(Resource* resource)
{
    // It should always return true in perfect world, but I never tested it.
    if (!resource)
        return true;
    const ea::string extension = GetExtension(resource->GetName());
    return extension == ".xml"
        || extension == ".glsl"
        || extension == ".hlsl";
}

static const char* checkDirs[] =
{
    "Fonts",
    "Materials",
    "Models",
    "Music",
    "Objects",
    "Particle",
    "PostProcess",
    "RenderPaths",
    "Scenes",
    "Scripts",
    "Sounds",
    "Shaders",
    "Techniques",
    "Textures",
    "UI",
    nullptr
};

static const SharedPtr<Resource> noResource;

ResourceCache::ResourceCache(Context* context) :
    Object(context),
    autoReloadResources_(false),
    returnFailedResources_(false),
    searchPackagesFirst_(true),
    isRouting_(false),
    finishBackgroundResourcesMs_(5)
{
    // Register Resource library object factories
    RegisterResourceLibrary(context_);

#ifdef URHO3D_THREADING
    // Create resource background loader. Its thread will start on the first background request
    backgroundLoader_ = new BackgroundLoader(this);
#endif

    // Subscribe BeginFrame for handling directory watchers and background loaded resource finalization
    SubscribeToEvent(E_BEGINFRAME, URHO3D_HANDLER(ResourceCache, HandleBeginFrame));
}

ResourceCache::~ResourceCache()
{
#ifdef URHO3D_THREADING
    // Shut down the background loader first
    backgroundLoader_.Reset();
#endif
}

bool ResourceCache::AddResourceDir(const ea::string& pathName, unsigned priority)
{
    MutexLock lock(resourceMutex_);

    auto* fileSystem = GetSubsystem<FileSystem>();
    if (!fileSystem || !fileSystem->DirExists(pathName))
    {
        URHO3D_LOGERROR("Could not open directory " + pathName);
        return false;
    }

    // Convert path to absolute
    ea::string fixedPath = SanitateResourceDirName(pathName);

    // Check that the same path does not already exist
    for (unsigned i = 0; i < resourceDirs_.size(); ++i)
    {
        if (!resourceDirs_[i].comparei(fixedPath))
            return true;
    }

    if (priority < resourceDirs_.size())
        resourceDirs_.insert_at(priority, fixedPath);
    else
        resourceDirs_.push_back(fixedPath);

    // If resource auto-reloading active, create a file watcher for the directory
    if (autoReloadResources_)
    {
        SharedPtr<FileWatcher> watcher(new FileWatcher(context_));
        watcher->StartWatching(fixedPath, true);
        fileWatchers_.push_back(watcher);
    }

    URHO3D_LOGINFO("Added resource path " + fixedPath);
    return true;
}

bool ResourceCache::AddPackageFile(PackageFile* package, unsigned priority)
{
    MutexLock lock(resourceMutex_);

    // Do not add packages that failed to load
    if (!package || !package->GetNumFiles())
    {
        URHO3D_LOGERRORF("Could not add package file %s due to load failure", package->GetName().c_str());
        return false;
    }

    if (priority < packages_.size())
        packages_.insert_at(priority, SharedPtr<PackageFile>(package));
    else
        packages_.push_back(SharedPtr<PackageFile>(package));

    URHO3D_LOGINFO("Added resource package " + package->GetName());
    return true;
}

bool ResourceCache::AddPackageFile(const ea::string& fileName, unsigned priority)
{
    SharedPtr<PackageFile> package(new PackageFile(context_));
    return package->Open(fileName) && AddPackageFile(package, priority);
}

bool ResourceCache::AddManualResource(Resource* resource)
{
    if (!resource)
    {
        URHO3D_LOGERROR("Null manual resource");
        return false;
    }

    const ea::string& name = resource->GetName();
    if (name.empty())
    {
        URHO3D_LOGERROR("Manual resource with empty name, can not add");
        return false;
    }

    resource->ResetUseTimer();
    resourceGroups_[resource->GetType()].resources_[resource->GetNameHash()] = resource;
    UpdateResourceGroup(resource->GetType());
    return true;
}

void ResourceCache::RemoveResourceDir(const ea::string& pathName)
{
    MutexLock lock(resourceMutex_);

    ea::string fixedPath = SanitateResourceDirName(pathName);

    for (unsigned i = 0; i < resourceDirs_.size(); ++i)
    {
        if (!resourceDirs_[i].comparei(fixedPath))
        {
            resourceDirs_.erase_at(i);
            // Remove the filewatcher with the matching path
            for (unsigned j = 0; j < fileWatchers_.size(); ++j)
            {
                if (!fileWatchers_[j]->GetPath().comparei(fixedPath))
                {
                    fileWatchers_.erase_at(j);
                    break;
                }
            }
            URHO3D_LOGINFO("Removed resource path " + fixedPath);
            return;
        }
    }
}

void ResourceCache::RemovePackageFile(PackageFile* package, bool releaseResources, bool forceRelease)
{
    MutexLock lock(resourceMutex_);

    for (auto i = packages_.begin(); i != packages_.end(); ++i)
    {
        if (i->Get() == package)
        {
            if (releaseResources)
                ReleasePackageResources(i->Get(), forceRelease);
            URHO3D_LOGINFO("Removed resource package " + (*i)->GetName());
            packages_.erase(i);
            return;
        }
    }
}

void ResourceCache::RemovePackageFile(const ea::string& fileName, bool releaseResources, bool forceRelease)
{
    MutexLock lock(resourceMutex_);

    // Compare the name and extension only, not the path
    ea::string fileNameNoPath = GetFileNameAndExtension(fileName);

    for (auto i = packages_.begin(); i != packages_.end(); ++i)
    {
        if (!GetFileNameAndExtension((*i)->GetName()).comparei(fileNameNoPath))
        {
            if (releaseResources)
                ReleasePackageResources(i->Get(), forceRelease);
            URHO3D_LOGINFO("Removed resource package " + (*i)->GetName());
            packages_.erase(i);
            return;
        }
    }
}

void ResourceCache::ReleaseResource(StringHash type, const ea::string& name, bool force)
{
    StringHash nameHash(name);
    const SharedPtr<Resource>& existingRes = FindResource(type, nameHash);
    if (!existingRes)
        return;

    // If other references exist, do not release, unless forced
    if ((existingRes.Refs() == 1 && existingRes.WeakRefs() == 0) || force)
    {
        resourceGroups_[type].resources_.erase(nameHash);
        UpdateResourceGroup(type);
    }
}

void ResourceCache::ReleaseResource(const ea::string& resourceName, bool force)
{
    // Some resources refer to others, like materials to textures. Repeat the release logic as many times as necessary to ensure
    // these get released. This is not necessary if forcing release
    bool released;
    do
    {
        released = false;

        for (auto i = resourceGroups_.begin(); i != resourceGroups_.end(); ++i)
        {
            for (auto j = i->second.resources_.begin(); j != i->second.resources_.end();)
            {
                auto current = i->second.resources_.find(resourceName);
                if (current != i->second.resources_.end())
                {
                    // If other references exist, do not release, unless forced
                    if ((current->second.Refs() == 1 && current->second.WeakRefs() == 0) || force)
                    {
                        j = i->second.resources_.erase(current);
                        released = true;
                        continue;
                    }
                }
                ++j;
            }
            if (released)
                UpdateResourceGroup(i->first);
        }

    } while (released && !force);
}

void ResourceCache::ReleaseResources(StringHash type, bool force)
{
    bool released = false;

    auto i = resourceGroups_.find(type);
    if (i != resourceGroups_.end())
    {
        for (auto j = i->second.resources_.begin();
             j != i->second.resources_.end();)
        {
            auto current = j++;
            // If other references exist, do not release, unless forced
            if ((current->second.Refs() == 1 && current->second.WeakRefs() == 0) || force)
            {
                i->second.resources_.erase(current);
                released = true;
            }
        }
    }

    if (released)
        UpdateResourceGroup(type);
}

void ResourceCache::ReleaseResources(StringHash type, const ea::string& partialName, bool force)
{
    bool released = false;

    auto i = resourceGroups_.find(type);
    if (i != resourceGroups_.end())
    {
        for (auto j = i->second.resources_.begin();
             j != i->second.resources_.end();)
        {
            auto current = j++;
            if (current->second->GetName().contains(partialName))
            {
                // If other references exist, do not release, unless forced
                if ((current->second.Refs() == 1 && current->second.WeakRefs() == 0) || force)
                {
                    i->second.resources_.erase(current);
                    released = true;
                }
            }
        }
    }

    if (released)
        UpdateResourceGroup(type);
}

void ResourceCache::ReleaseResources(const ea::string& partialName, bool force)
{
    // Some resources refer to others, like materials to textures. Repeat the release logic as many times as necessary to ensure
    // these get released. This is not necessary if forcing release
    bool released;
    do
    {
        released = false;

        for (auto i = resourceGroups_.begin(); i !=
            resourceGroups_.end(); ++i)
        {
            for (auto j = i->second.resources_.begin();
                 j != i->second.resources_.end();)
            {
                auto current = j++;
                if (current->second->GetName().contains(partialName))
                {
                    // If other references exist, do not release, unless forced
                    if ((current->second.Refs() == 1 && current->second.WeakRefs() == 0) || force)
                    {
                        i->second.resources_.erase(current);
                        released = true;
                    }
                }
            }
            if (released)
                UpdateResourceGroup(i->first);
        }

    } while (released && !force);
}

void ResourceCache::ReleaseAllResources(bool force)
{
    bool released;
    do
    {
        released = false;

        for (auto i = resourceGroups_.begin();
             i != resourceGroups_.end(); ++i)
        {
            for (auto j = i->second.resources_.begin();
                 j != i->second.resources_.end();)
            {
                auto current = j++;
                // If other references exist, do not release, unless forced
                if ((current->second.Refs() == 1 && current->second.WeakRefs() == 0) || force)
                {
                    i->second.resources_.erase(current);
                    released = true;
                }
            }
            if (released)
                UpdateResourceGroup(i->first);
        }

    } while (released && !force);
}

bool ResourceCache::ReloadResource(const ea::string_view resourceName)
{
    if (Resource* resource = FindResource(StringHash::ZERO, resourceName))
        return ReloadResource(resource);
    return false;
}

bool ResourceCache::ReloadResource(Resource* resource)
{
    if (!resource)
        return false;

    resource->SendEvent(E_RELOADSTARTED);

    bool success = false;
    SharedPtr<File> file = GetFile(resource->GetName());
    if (file)
        success = resource->Load(*(file.Get()));

    if (success)
    {
        resource->ResetUseTimer();
        UpdateResourceGroup(resource->GetType());
        resource->SendEvent(E_RELOADFINISHED);
        return true;
    }

    // If reloading failed, do not remove the resource from cache, to allow for a new live edit to
    // attempt loading again
    resource->SendEvent(E_RELOADFAILED);
    return false;
}

void ResourceCache::ReloadResourceWithDependencies(const ea::string& fileName)
{
    StringHash fileNameHash(fileName);
    // If the filename is a resource we keep track of, reload it
    const SharedPtr<Resource>& resource = FindResource(fileNameHash);
    if (resource)
    {
        URHO3D_LOGDEBUG("Reloading changed resource " + fileName);
        ReloadResource(resource.Get());
    }
    // Always perform dependency resource check for resource loaded from XML file as it could be used in inheritance
    if (NeedToReloadDependencies(resource))
    {
        // Check if this is a dependency resource, reload dependents
        auto j = dependentResources_.find(
            fileNameHash);
        if (j != dependentResources_.end())
        {
            // Reloading a resource may modify the dependency tracking structure. Therefore collect the
            // resources we need to reload first
            ea::vector<SharedPtr<Resource> > dependents;
            dependents.reserve(j->second.size());

            for (auto k = j->second.begin(); k != j->second.end(); ++k)
            {
                const SharedPtr<Resource>& dependent = FindResource(*k);
                if (dependent)
                    dependents.push_back(dependent);
            }

            for (unsigned k = 0; k < dependents.size(); ++k)
            {
                URHO3D_LOGDEBUG("Reloading resource " + dependents[k]->GetName() + " depending on " + fileName);
                ReloadResource(dependents[k].Get());
            }
        }
    }
}

void ResourceCache::SetMemoryBudget(StringHash type, unsigned long long budget)
{
    resourceGroups_[type].memoryBudget_ = budget;
}

void ResourceCache::SetAutoReloadResources(bool enable)
{
    if (enable != autoReloadResources_)
    {
        if (enable)
        {
            for (unsigned i = 0; i < resourceDirs_.size(); ++i)
            {
                SharedPtr<FileWatcher> watcher(new FileWatcher(context_));
                watcher->StartWatching(resourceDirs_[i], true);
                fileWatchers_.push_back(watcher);
            }
        }
        else
            fileWatchers_.clear();

        autoReloadResources_ = enable;
    }
}

void ResourceCache::AddResourceRouter(ResourceRouter* router, bool addAsFirst)
{
    // Check for duplicate
    for (unsigned i = 0; i < resourceRouters_.size(); ++i)
    {
        if (resourceRouters_[i] == router)
            return;
    }

    if (addAsFirst)
        resourceRouters_.push_front(SharedPtr<ResourceRouter>(router));
    else
        resourceRouters_.push_back(SharedPtr<ResourceRouter>(router));
}

void ResourceCache::RemoveResourceRouter(ResourceRouter* router)
{
    for (unsigned i = 0; i < resourceRouters_.size(); ++i)
    {
        if (resourceRouters_[i] == router)
        {
            resourceRouters_.erase_at(i);
            return;
        }
    }
}

SharedPtr<File> ResourceCache::GetFile(const ea::string& name, bool sendEventOnFailure)
{
    MutexLock lock(resourceMutex_);

    ea::string sanitatedName = SanitateResourceName(name);
    RouteResourceName(sanitatedName, RESOURCE_GETFILE);

    if (sanitatedName.length())
    {
        File* file = nullptr;

        if (searchPackagesFirst_)
        {
            file = SearchPackages(sanitatedName);
            if (!file)
                file = SearchResourceDirs(sanitatedName);
        }
        else
        {
            file = SearchResourceDirs(sanitatedName);
            if (!file)
                file = SearchPackages(sanitatedName);
        }

        if (file)
            return SharedPtr<File>(file);
    }

    if (sendEventOnFailure)
    {
        if (resourceRouters_.size() && sanitatedName.empty() && !name.empty())
            URHO3D_LOGERROR("Resource request " + name + " was blocked");
        else
            URHO3D_LOGERROR("Could not find resource " + sanitatedName);

        if (Thread::IsMainThread())
        {
            using namespace ResourceNotFound;

            VariantMap& eventData = GetEventDataMap();
            eventData[P_RESOURCENAME] = sanitatedName.length() ? sanitatedName : name;
            SendEvent(E_RESOURCENOTFOUND, eventData);
        }
    }

    return SharedPtr<File>();
}

Resource* ResourceCache::GetExistingResource(StringHash type, const ea::string& name)
{
    ea::string sanitatedName = SanitateResourceName(name);

    if (!Thread::IsMainThread())
    {
        URHO3D_LOGERROR("Attempted to get resource " + sanitatedName + " from outside the main thread");
        return nullptr;
    }

    // If empty name, return null pointer immediately
    if (sanitatedName.empty())
        return nullptr;

    StringHash nameHash(sanitatedName);

    const SharedPtr<Resource>& existing = type == StringHash::ZERO ? FindResource(type, nameHash) : FindResource(nameHash);
    return existing;
}

Resource* ResourceCache::GetResource(StringHash type, const ea::string& name, bool sendEventOnFailure)
{
    ea::string sanitatedName = SanitateResourceName(name);

    if (!Thread::IsMainThread())
    {
        URHO3D_LOGERROR("Attempted to get resource " + sanitatedName + " from outside the main thread");
        return nullptr;
    }

    // If empty name, return null pointer immediately
    if (sanitatedName.empty())
        return nullptr;

    StringHash nameHash(sanitatedName);

#ifdef URHO3D_THREADING
    // Check if the resource is being background loaded but is now needed immediately
    backgroundLoader_->WaitForResource(type, nameHash);
#endif

    const SharedPtr<Resource>& existing = FindResource(type, nameHash);
    if (existing)
        return existing;

    SharedPtr<Resource> resource;
    // Make sure the pointer is non-null and is a Resource subclass
    resource = DynamicCast<Resource>(context_->CreateObject(type));
    if (!resource)
    {
        URHO3D_LOGERROR("Could not load unknown resource type " + type.ToString());

        if (sendEventOnFailure)
        {
            using namespace UnknownResourceType;

            VariantMap& eventData = GetEventDataMap();
            eventData[P_RESOURCETYPE] = type;
            SendEvent(E_UNKNOWNRESOURCETYPE, eventData);
        }

        return nullptr;
    }

    // Attempt to load the resource
    SharedPtr<File> file = GetFile(sanitatedName, sendEventOnFailure);
    if (!file)
        return nullptr;   // Error is already logged

    URHO3D_LOGDEBUG("Loading resource " + sanitatedName);
    resource->SetName(sanitatedName);
    resource->SetAbsoluteFileName(file->GetAbsoluteName());

    if (!resource->Load(*(file.Get())))
    {
        // Error should already been logged by corresponding resource descendant class
        if (sendEventOnFailure)
        {
            using namespace LoadFailed;

            VariantMap& eventData = GetEventDataMap();
            eventData[P_RESOURCENAME] = sanitatedName;
            SendEvent(E_LOADFAILED, eventData);
        }

        if (!returnFailedResources_)
            return nullptr;
    }

    // Store to cache
    resource->ResetUseTimer();
    resourceGroups_[type].resources_[nameHash] = resource;
    UpdateResourceGroup(type);

    return resource;
}

bool ResourceCache::BackgroundLoadResource(StringHash type, const ea::string& name, bool sendEventOnFailure, Resource* caller)
{
#ifdef URHO3D_THREADING
    // If empty name, fail immediately
    ea::string sanitatedName = SanitateResourceName(name);
    if (sanitatedName.empty())
        return false;

    // First check if already exists as a loaded resource
    StringHash nameHash(sanitatedName);
    if (FindResource(type, nameHash) != noResource)
        return false;

    return backgroundLoader_->QueueResource(type, sanitatedName, sendEventOnFailure, caller);
#else
    // When threading not supported, fall back to synchronous loading
    return GetResource(type, name, sendEventOnFailure);
#endif
}

SharedPtr<Resource> ResourceCache::GetTempResource(StringHash type, const ea::string& name, bool sendEventOnFailure)
{
    ea::string sanitatedName = SanitateResourceName(name);

    // If empty name, return null pointer immediately
    if (sanitatedName.empty())
        return SharedPtr<Resource>();

    SharedPtr<Resource> resource;
    // Make sure the pointer is non-null and is a Resource subclass
    resource = DynamicCast<Resource>(context_->CreateObject(type));
    if (!resource)
    {
        URHO3D_LOGERROR("Could not load unknown resource type " + type.ToString());

        if (sendEventOnFailure)
        {
            using namespace UnknownResourceType;

            VariantMap& eventData = GetEventDataMap();
            eventData[P_RESOURCETYPE] = type;
            SendEvent(E_UNKNOWNRESOURCETYPE, eventData);
        }

        return SharedPtr<Resource>();
    }

    // Attempt to load the resource
    SharedPtr<File> file = GetFile(sanitatedName, sendEventOnFailure);
    if (!file)
        return SharedPtr<Resource>();  // Error is already logged

    URHO3D_LOGDEBUG("Loading temporary resource " + sanitatedName);
    resource->SetName(file->GetName());
    resource->SetAbsoluteFileName(file->GetAbsoluteName());

    if (!resource->Load(*(file.Get())))
    {
        // Error should already been logged by corresponding resource descendant class
        if (sendEventOnFailure)
        {
            using namespace LoadFailed;

            VariantMap& eventData = GetEventDataMap();
            eventData[P_RESOURCENAME] = sanitatedName;
            SendEvent(E_LOADFAILED, eventData);
        }

        return SharedPtr<Resource>();
    }

    return resource;
}

unsigned ResourceCache::GetNumBackgroundLoadResources() const
{
#ifdef URHO3D_THREADING
    return backgroundLoader_->GetNumQueuedResources();
#else
    return 0;
#endif
}

void ResourceCache::GetResources(ea::vector<Resource*>& result, StringHash type) const
{
    result.clear();
    auto i = resourceGroups_.find(type);
    if (i != resourceGroups_.end())
    {
        for (auto j = i->second.resources_.begin();
             j != i->second.resources_.end(); ++j)
            result.push_back(j->second.Get());
    }
}

bool ResourceCache::Exists(const ea::string& name) const
{
    MutexLock lock(resourceMutex_);

    ea::string sanitatedName = SanitateResourceName(name);
    RouteResourceName(sanitatedName, RESOURCE_CHECKEXISTS);

    if (sanitatedName.empty())
        return false;

    for (unsigned i = 0; i < packages_.size(); ++i)
    {
        if (packages_[i]->Exists(sanitatedName))
            return true;
    }

    auto* fileSystem = GetSubsystem<FileSystem>();
    for (unsigned i = 0; i < resourceDirs_.size(); ++i)
    {
        if (fileSystem->FileExists(resourceDirs_[i] + sanitatedName))
            return true;
    }

    // Fallback using absolute path
    return fileSystem->FileExists(sanitatedName);
}

unsigned long long ResourceCache::GetMemoryBudget(StringHash type) const
{
    auto i = resourceGroups_.find(type);
    return i != resourceGroups_.end() ? i->second.memoryBudget_ : 0;
}

unsigned long long ResourceCache::GetMemoryUse(StringHash type) const
{
    auto i = resourceGroups_.find(type);
    return i != resourceGroups_.end() ? i->second.memoryUse_ : 0;
}

unsigned long long ResourceCache::GetTotalMemoryUse() const
{
    unsigned long long total = 0;
    for (auto i = resourceGroups_.begin(); i !=
        resourceGroups_.end(); ++i)
        total += i->second.memoryUse_;
    return total;
}

ea::string ResourceCache::GetResourceFileName(const ea::string& name) const
{
    MutexLock lock(resourceMutex_);

    auto* fileSystem = GetSubsystem<FileSystem>();
    for (unsigned i = 0; i < resourceDirs_.size(); ++i)
    {
        if (fileSystem->FileExists(resourceDirs_[i] + name))
            return resourceDirs_[i] + name;
    }

    if (IsAbsolutePath(name) && fileSystem->FileExists(name))
        return name;
    else
        return ea::string();
}

ResourceRouter* ResourceCache::GetResourceRouter(unsigned index) const
{
    return index < resourceRouters_.size() ? resourceRouters_[index] : nullptr;
}

ea::string ResourceCache::GetPreferredResourceDir(const ea::string& path) const
{
    ea::string fixedPath = AddTrailingSlash(path);

    bool pathHasKnownDirs = false;
    bool parentHasKnownDirs = false;

    auto* fileSystem = GetSubsystem<FileSystem>();

    for (unsigned i = 0; checkDirs[i] != nullptr; ++i)
    {
        if (fileSystem->DirExists(fixedPath + checkDirs[i]))
        {
            pathHasKnownDirs = true;
            break;
        }
    }
    if (!pathHasKnownDirs)
    {
        ea::string parentPath = GetParentPath(fixedPath);
        for (unsigned i = 0; checkDirs[i] != nullptr; ++i)
        {
            if (fileSystem->DirExists(parentPath + checkDirs[i]))
            {
                parentHasKnownDirs = true;
                break;
            }
        }
        // If path does not have known subdirectories, but the parent path has, use the parent instead
        if (parentHasKnownDirs)
            fixedPath = parentPath;
    }

    return fixedPath;
}

ea::string ResourceCache::SanitateResourceName(const ea::string& name) const
{
    // Sanitate unsupported constructs from the resource name
    ea::string sanitatedName = GetInternalPath(name);
    sanitatedName.replace("../", "");
    sanitatedName.replace("./", "");

    // If the path refers to one of the resource directories, normalize the resource name
    auto* fileSystem = GetSubsystem<FileSystem>();
    if (resourceDirs_.size())
    {
        ea::string namePath = GetPath(sanitatedName);
        ea::string exePath = fileSystem->GetProgramDir().replaced("/./", "/");
        for (unsigned i = 0; i < resourceDirs_.size(); ++i)
        {
            ea::string relativeResourcePath = resourceDirs_[i];
            if (relativeResourcePath.starts_with(exePath))
                relativeResourcePath = relativeResourcePath.substr(exePath.length());

            if (namePath.starts_with(resourceDirs_[i], false))
                namePath = namePath.substr(resourceDirs_[i].length());
            else if (namePath.starts_with(relativeResourcePath, false))
                namePath = namePath.substr(relativeResourcePath.length());
        }

        sanitatedName = namePath + GetFileNameAndExtension(sanitatedName);
    }

    sanitatedName.trim();
    return sanitatedName;
}

ea::string ResourceCache::SanitateResourceDirName(const ea::string& name) const
{
    ea::string fixedPath = AddTrailingSlash(name);
    if (!IsAbsolutePath(fixedPath))
        fixedPath = GetSubsystem<FileSystem>()->GetCurrentDir() + fixedPath;

    // Sanitate away /./ construct
    fixedPath.replace("/./", "/");

    fixedPath.trim();
    return fixedPath;
}

void ResourceCache::StoreResourceDependency(Resource* resource, const ea::string& dependency)
{
    if (!resource)
        return;

    MutexLock lock(resourceMutex_);

    StringHash nameHash(resource->GetName());
    ea::hash_set<StringHash>& dependents = dependentResources_[dependency];
    dependents.insert(nameHash);
}

void ResourceCache::ResetDependencies(Resource* resource)
{
    if (!resource)
        return;

    MutexLock lock(resourceMutex_);

    StringHash nameHash(resource->GetName());

    for (auto i = dependentResources_.begin(); i !=
        dependentResources_.end();)
    {
        ea::hash_set<StringHash>& dependents = i->second;
        dependents.erase(nameHash);
        if (dependents.empty())
            i = dependentResources_.erase(i);
        else
            ++i;
    }
}

ea::string ResourceCache::PrintMemoryUsage() const
{
    ea::string output = "Resource Type                 Cnt       Avg       Max    Budget     Total\n\n";
    char outputLine[256];

    unsigned totalResourceCt = 0;
    unsigned long long totalLargest = 0;
    unsigned long long totalAverage = 0;
    unsigned long long totalUse = GetTotalMemoryUse();

    for (auto cit = resourceGroups_.begin(); cit !=
        resourceGroups_.end(); ++cit)
    {
        const unsigned resourceCt = cit->second.resources_.size();
        unsigned long long average = 0;
        if (resourceCt > 0)
            average = cit->second.memoryUse_ / resourceCt;
        else
            average = 0;
        unsigned long long largest = 0;
        for (auto resIt = cit->second.resources_.begin(); resIt != cit->second.resources_.end(); ++resIt)
        {
            if (resIt->second->GetMemoryUse() > largest)
                largest = resIt->second->GetMemoryUse();
            if (largest > totalLargest)
                totalLargest = largest;
        }

        totalResourceCt += resourceCt;

        const ea::string countString = ea::to_string(cit->second.resources_.size());
        const ea::string memUseString = GetFileSizeString(average);
        const ea::string memMaxString = GetFileSizeString(largest);
        const ea::string memBudgetString = GetFileSizeString(cit->second.memoryBudget_);
        const ea::string memTotalString = GetFileSizeString(cit->second.memoryUse_);
        const ea::string resTypeName = context_->GetTypeName(cit->first);

        memset(outputLine, ' ', 256);
        outputLine[255] = 0;
        sprintf(outputLine, "%-28s %4s %9s %9s %9s %9s\n", resTypeName.c_str(), countString.c_str(), memUseString.c_str(), memMaxString.c_str(), memBudgetString.c_str(), memTotalString.c_str());

        output += ((const char*)outputLine);
    }

    if (totalResourceCt > 0)
        totalAverage = totalUse / totalResourceCt;

    const ea::string countString = ea::to_string(totalResourceCt);
    const ea::string memUseString = GetFileSizeString(totalAverage);
    const ea::string memMaxString = GetFileSizeString(totalLargest);
    const ea::string memTotalString = GetFileSizeString(totalUse);

    memset(outputLine, ' ', 256);
    outputLine[255] = 0;
    sprintf(outputLine, "%-28s %4s %9s %9s %9s %9s\n", "All", countString.c_str(), memUseString.c_str(), memMaxString.c_str(), "-", memTotalString.c_str());
    output += ((const char*)outputLine);

    return output;
}

const SharedPtr<Resource>& ResourceCache::FindResource(StringHash type, StringHash nameHash)
{
    MutexLock lock(resourceMutex_);

    auto i = resourceGroups_.find(type);
    if (i == resourceGroups_.end())
        return noResource;
    auto j = i->second.resources_.find(nameHash);
    if (j == i->second.resources_.end())
        return noResource;

    return j->second;
}

const SharedPtr<Resource>& ResourceCache::FindResource(StringHash nameHash)
{
    MutexLock lock(resourceMutex_);

    for (auto i = resourceGroups_.begin(); i !=
        resourceGroups_.end(); ++i)
    {
        auto j = i->second.resources_.find(nameHash);
        if (j != i->second.resources_.end())
            return j->second;
    }

    return noResource;
}

void ResourceCache::ReleasePackageResources(PackageFile* package, bool force)
{
    ea::hash_set<StringHash> affectedGroups;

    const ea::unordered_map<ea::string, PackageEntry>& entries = package->GetEntries();
    for (auto i = entries.begin(); i != entries.end(); ++i)
    {
        StringHash nameHash(i->first);

        // We do not know the actual resource type, so search all type containers
        for (auto j = resourceGroups_.begin(); j !=
            resourceGroups_.end(); ++j)
        {
            auto k = j->second.resources_.find(nameHash);
            if (k != j->second.resources_.end())
            {
                // If other references exist, do not release, unless forced
                if ((k->second.Refs() == 1 && k->second.WeakRefs() == 0) || force)
                {
                    j->second.resources_.erase(k);
                    affectedGroups.insert(j->first);
                }
                break;
            }
        }
    }

    for (auto i = affectedGroups.begin(); i != affectedGroups.end(); ++i)
        UpdateResourceGroup(*i);
}

void ResourceCache::UpdateResourceGroup(StringHash type)
{
    auto i = resourceGroups_.find(type);
    if (i == resourceGroups_.end())
        return;

    for (;;)
    {
        unsigned totalSize = 0;
        unsigned oldestTimer = 0;
        auto oldestResource = i->second.resources_.end();

        for (auto j = i->second.resources_.begin();
             j != i->second.resources_.end(); ++j)
        {
            totalSize += j->second->GetMemoryUse();
            unsigned useTimer = j->second->GetUseTimer();
            if (useTimer > oldestTimer)
            {
                oldestTimer = useTimer;
                oldestResource = j;
            }
        }

        i->second.memoryUse_ = totalSize;

        // If memory budget defined and is exceeded, remove the oldest resource and loop again
        // (resources in use always return a zero timer and can not be removed)
        if (i->second.memoryBudget_ && i->second.memoryUse_ > i->second.memoryBudget_ &&
            oldestResource != i->second.resources_.end())
        {
            URHO3D_LOGDEBUG("Resource group " + oldestResource->second->GetTypeName() + " over memory budget, releasing resource " +
                     oldestResource->second->GetName());
            i->second.resources_.erase(oldestResource);
        }
        else
            break;
    }
}

void ResourceCache::HandleBeginFrame(StringHash eventType, VariantMap& eventData)
{
    for (unsigned i = 0; i < fileWatchers_.size(); ++i)
    {
        FileChange change;
        while (fileWatchers_[i]->GetNextChange(change))
        {
            auto it = ignoreResourceAutoReload_.find(change.fileName_);
            if (it != ignoreResourceAutoReload_.end())
            {
                ignoreResourceAutoReload_.erase(it);
                continue;
            }

            ReloadResourceWithDependencies(change.fileName_);

            // Finally send a general file changed event even if the file was not a tracked resource
            using namespace FileChanged;

            VariantMap& eventData = GetEventDataMap();
            eventData[P_FILENAME] = fileWatchers_[i]->GetPath() + change.fileName_;
            eventData[P_RESOURCENAME] = change.fileName_;
            SendEvent(E_FILECHANGED, eventData);
        }
    }

    // Check for background loaded resources that can be finished
#ifdef URHO3D_THREADING
    {
        URHO3D_PROFILE("FinishBackgroundResources");
        backgroundLoader_->FinishResources(finishBackgroundResourcesMs_);
    }
#endif
}

File* ResourceCache::SearchResourceDirs(const ea::string& name)
{
    auto* fileSystem = GetSubsystem<FileSystem>();
    for (unsigned i = 0; i < resourceDirs_.size(); ++i)
    {
        if (fileSystem->FileExists(resourceDirs_[i] + name))
        {
            // Construct the file first with full path, then rename it to not contain the resource path,
            // so that the file's sanitatedName can be used in further GetFile() calls (for example over the network)
            File* file(new File(context_, resourceDirs_[i] + name));
            file->SetName(name);
            return file;
        }
    }

    // Fallback using absolute path
    if (fileSystem->FileExists(name))
        return new File(context_, name);

    return nullptr;
}

File* ResourceCache::SearchPackages(const ea::string& name)
{
    for (unsigned i = 0; i < packages_.size(); ++i)
    {
        if (packages_[i]->Exists(name))
            return new File(context_, packages_[i], name);
    }

    return nullptr;
}

void RegisterResourceLibrary(Context* context)
{
    BinaryFile::RegisterObject(context);
    Image::RegisterObject(context);
    ImageCube::RegisterObject(context);
    JSONFile::RegisterObject(context);
    PListFile::RegisterObject(context);
    XMLFile::RegisterObject(context);
}

void ResourceCache::Scan(ea::vector<ea::string>& result, const ea::string& pathName, const ea::string& filter, unsigned flags, bool recursive) const
{
    ea::vector<ea::string> interimResult;

    for (unsigned i = 0; i < packages_.size(); ++i)
    {
        packages_[i]->Scan(interimResult, pathName, filter, recursive);
        result.insert(result.end(), interimResult.begin(), interimResult.end());
    }

    FileSystem* fileSystem = GetSubsystem<FileSystem>();
    for (unsigned i = 0; i < resourceDirs_.size(); ++i)
    {
        fileSystem->ScanDir(interimResult, resourceDirs_[i] + pathName, filter, flags, recursive);
        result.insert(result.end(), interimResult.begin(), interimResult.end());
    }

    // Filtering copied from PackageFile::Scan().
    ea::string sanitizedPath = GetSanitizedPath(pathName);
    ea::string filterExtension;
    unsigned dotPos = filter.find_last_of('.');
    if (dotPos != ea::string::npos)
        filterExtension = filter.substr(dotPos);
    if (filterExtension.contains('*'))
        filterExtension.clear();

    bool caseSensitive = true;
#ifdef _WIN32
    // On Windows ignore case in string comparisons
    caseSensitive = false;
#endif

    // Manual resources.
    for (auto group = resourceGroups_.begin(); group != resourceGroups_.end(); ++group)
    {
        for (auto res = group->second.resources_.begin(); res != group->second.resources_.end(); ++res)
        {
            ea::string entryName = GetSanitizedPath(res->second->GetName());
            if ((filterExtension.empty() || entryName.ends_with(filterExtension, caseSensitive)) &&
                entryName.starts_with(sanitizedPath, caseSensitive))
            {
                // Manual resources do not exist in resource dirs.
                bool isPhysicalResource = false;
                for (unsigned i = 0; i < resourceDirs_.size() && !isPhysicalResource; ++i)
                    isPhysicalResource = fileSystem->FileExists(resourceDirs_[i] + pathName);

                if (!isPhysicalResource)
                {
                    int index = entryName.find("/", sanitizedPath.length());
                    if (flags & SCAN_DIRS && index >= 0)
                        result.emplace_back(entryName.substr(sanitizedPath.length(), index - sanitizedPath.length()));
                    else if (flags & SCAN_FILES && index == -1)
                        result.emplace_back(entryName.substr(sanitizedPath.length()));
                }
            }
        }
    }
}

ea::string ResourceCache::PrintResources(const ea::string& typeName) const
{

    StringHash typeNameHash(typeName);

    ea::string output = "Resource Type         Refs   WeakRefs  Name\n\n";

    for (auto cit = resourceGroups_.begin(); cit !=
        resourceGroups_.end(); ++cit)
    {
        for (auto resIt = cit->second.resources_.begin(); resIt != cit->second.resources_.end(); ++resIt)
        {
            Resource* resource = resIt->second;

            // filter
            if (typeName.length() && resource->GetType() != typeNameHash)
                continue;

            output.append_sprintf("%s     %i     %i     %s\n", resource->GetTypeName().c_str(), resource->Refs(),
                resource->WeakRefs(), resource->GetName().c_str());
        }

    }

    return output;
}

bool ResourceCache::RenameResource(const ea::string& source, const ea::string& destination)
{
    if (!packages_.empty())
    {
        URHO3D_LOGERROR("Renaming resources not supported while packages are in use.");
        return false;
    }

    if (!IsAbsolutePath(source) || !IsAbsolutePath(destination))
    {
        URHO3D_LOGERROR("Renaming resources requires absolute paths.");
        return false;
    }

    auto* fileSystem = GetSubsystem<FileSystem>();

    if (!fileSystem->Exists(source))
    {
        URHO3D_LOGERROR("Source path does not exist.");
        return false;
    }

    if (fileSystem->Exists(destination))
    {
        URHO3D_LOGERROR("Destination path already exists.");
        return false;
    }

    ea::string resourceName;
    ea::string destinationName;
    bool dirMode = fileSystem->DirExists(source);
    for (const auto& dir : resourceDirs_)
    {
        if (source.starts_with(dir))
            resourceName = source.substr(dir.length());
        if (destination.starts_with(dir))
            destinationName = destination.substr(dir.length());
    }

    if (dirMode)
    {
        resourceName = AddTrailingSlash(resourceName);
        destinationName = AddTrailingSlash(destinationName);
    }

    if (resourceName.empty())
    {
        URHO3D_LOGERRORF("'%s' does not exist in resource path.", source.c_str());
        return false;
    }

    // Ensure parent path exists
    if (!fileSystem->CreateDirsRecursive(GetParentPath(destination)))
        return false;

    if (!fileSystem->Rename(source, destination))
    {
        URHO3D_LOGERRORF("Renaming '%s' to '%s' failed.", source.c_str(), destination.c_str());
        return false;
    }

    const ea::string sourceDir = AddTrailingSlash(source);
    const ea::string destinationDir = AddTrailingSlash(destination);

    // Update loaded resource information
    for (auto& groupPair : resourceGroups_)
    {
        bool movedAny = false;
        auto resourcesCopy = groupPair.second.resources_;
        for (auto& resourcePair : resourcesCopy)
        {
            SharedPtr<Resource> resource = resourcePair.second;
            ea::string newName;
            ea::string newNativeFileName;
            if (dirMode)
            {
                if (!resource->GetName().starts_with(resourceName))
                    continue;
                newName = destinationName + resource->GetName().substr(resourceName.length());

                newNativeFileName = resource->GetNativeFileName();
                if (newNativeFileName.starts_with(sourceDir))
                    newNativeFileName.replace(0u, sourceDir.length(), destinationDir);
            }
            else if (resource->GetName() != resourceName)
                continue;
            else
            {
                newName = destinationName;
                newNativeFileName = destination;
            }

            if (autoReloadResources_)
            {
                ignoreResourceAutoReload_.emplace_back(destinationName);
                ignoreResourceAutoReload_.emplace_back(resource->GetName());
            }

            groupPair.second.resources_.erase(resource->GetNameHash());
            resource->SetName(newName);
            resource->SetAbsoluteFileName(newNativeFileName);
            groupPair.second.resources_[resource->GetNameHash()] = resource;
            movedAny = true;

            using namespace ResourceRenamed;
            SendEvent(E_RESOURCERENAMED, P_FROM, resourceName, P_TO, destinationName);
        }
        if (movedAny)
            UpdateResourceGroup(groupPair.first);
    }

    if (dirMode)
    {
        using namespace ResourceRenamed;
        SendEvent(E_RESOURCERENAMED, P_FROM, resourceName, P_TO, destinationName);
    }

    return true;
}

void ResourceCache::IgnoreResourceReload(const ea::string& name)
{
    ignoreResourceAutoReload_.emplace_back(name);
}

void ResourceCache::IgnoreResourceReload(const Resource* resource)
{
    IgnoreResourceReload(resource->GetName());
}

void ResourceCache::RouteResourceName(ea::string& name, ResourceRequest requestType) const
{
    name = SanitateResourceName(name);
    if (!isRouting_)
    {
        isRouting_ = true;
        for (unsigned i = 0; i < resourceRouters_.size(); ++i)
            resourceRouters_[i]->Route(name, requestType);
        isRouting_ = false;
    }
}

void ResourceCache::Clear()
{
    resourceGroups_.clear();
    dependentResources_.clear();
}

}
