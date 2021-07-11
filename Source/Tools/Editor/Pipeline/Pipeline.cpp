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

#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Core/WorkQueue.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Engine/EngineDefs.h>
#include <Urho3D/Engine/ApplicationSettings.h>
#include <Urho3D/Resource/JSONValue.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Resource/ResourceEvents.h>
#include <Urho3D/IO/ArchiveSerialization.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/IO/PackageFile.h>
#include <Urho3D/Resource/JSONFile.h>
#include <Urho3D/Resource/JSONArchive.h>

#include <Toolbox/SystemUI/Widgets.h>
#include <IconFontCppHeaders/IconsFontAwesome5.h>

#include <EASTL/sort.h>

#include "Editor.h"
#include "EditorEvents.h"
#include "Project.h"
#include "Pipeline/Pipeline.h"
#include "Plugins/PluginManager.h"
#include "Plugins/ModulePlugin.h"
#include "Tabs/InspectorTab.h"
#include "Tabs/ResourceTab.h"

namespace Urho3D
{

Pipeline::Pipeline(Context* context)
    : Object(context)
    , watcher_(context)
{
    if (context_->GetSubsystem<Engine>()->IsHeadless())
        return;

    SubscribeToEvent(E_ENDFRAME, &Pipeline::OnEndFrame);
    SubscribeToEvent(E_RESOURCERENAMED, [this](StringHash, VariantMap& args) {
        using namespace ResourceRenamed;
        ea::string from = args[P_FROM].GetString();
        ea::string to = args[P_TO].GetString();

        if (from.ends_with("/"))
        {
            ea::unordered_map<ea::string, SharedPtr<Asset>> assets{};
            for (auto it = assets_.begin(); it != assets_.end();)
            {
                if (it->first.starts_with(from))
                {
                    assets[to + it->first.substr(from.length())] = it->second;
                    it = assets_.erase(it);
                }
                else
                    ++it;
            }
            for (const auto& pair : assets)
                assets_[pair.first] = pair.second;
        }
        else
        {
            auto it = assets_.find(from);
            if (it != assets_.end())
            {
                assets_[to] = it->second;
                assets_.erase(it);
            }
        }
    });
    SubscribeToEvent(E_UPDATE, &Pipeline::OnUpdate);
    SubscribeToEvent(E_EDITORIMPORTERATTRIBUTEMODIFIED, &Pipeline::OnImporterModified);
    SubscribeToEvent(E_RESOURCEBROWSERDELETE, [this](StringHash, VariantMap& args) {
        using namespace ResourceBrowserDelete;
        if (Asset* asset = GetAsset(args[P_NAME].GetString(), false))
            assets_.erase(asset->GetName());
    });

    auto* editor = GetSubsystem<Editor>();
    editor->settingsTabs_.Subscribe(this, &Pipeline::RenderSettingsUI);
}

void Pipeline::RegisterObject(Context* context)
{
    context->RegisterFactory<Pipeline>();
}

void Pipeline::EnableWatcher()
{
    auto* project = GetSubsystem<Project>();
    auto* fs = GetSubsystem<FileSystem>();

    if (!fs->DirExists(project->GetCachePath()))
        fs->CreateDirsRecursive(project->GetCachePath());

    watcher_.StopWatching();
    for (const ea::string& resourceDir : project->GetResourcePaths())
    {
        ea::string absolutePath = project->GetProjectPath() + resourceDir;
        if (!fs->DirExists(absolutePath))
            fs->CreateDirsRecursive(absolutePath);
        watcher_.StartWatching(absolutePath, true);
    }
}

void Pipeline::OnEndFrame(StringHash, VariantMap&)
{
    FileChange entry;
    while (watcher_.GetNextChange(entry))
    {
        if (entry.fileName_.ends_with(".asset"))
            continue;

        if (Asset* asset = GetAsset(entry.fileName_))
            ScheduleImport(asset);

        onResourceChanged_(this, entry);
    }

    if (!dirtyAssets_.empty())
    {
        auto* inspector = GetSubsystem<InspectorTab>();
        MutexLock lock(mutex_);
        Asset* dirty = dirtyAssets_.back();
        dirty->Save();
        if (inspector->IsInspected(dirty))
        {
            // Asset import may introduce new imported resources which we want to be appear in inspection if importing
            // asset was selected when import triggered.
            dirty->Inspect();
        }
        dirtyAssets_.pop_back();
    }
}

void Pipeline::ClearCache(const ea::string& resourceName)
{
    for (const auto& asset : assets_)
        asset.second->ClearCache();
}

Asset* Pipeline::GetAsset(const ea::string& resourceName, bool autoCreate)
{
    MutexLock lock(mutex_);

    if (resourceName.empty() || resourceName.ends_with(".asset"))
        return nullptr;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* project = GetSubsystem<Project>();
    auto* fs = GetSubsystem<FileSystem>();

    for (const ea::string& resourceDir : project->GetResourcePaths())
    {
        ea::string resourcePath = Format("{}{}{}", project->GetProjectPath(), resourceDir, resourceName);
        ea::string resourceDirName;
        if (fs->DirExists(resourcePath))
        {
            resourcePath = AddTrailingSlash(resourcePath);
            resourceDirName = AddTrailingSlash(resourceName);
        }
        const ea::string& actualResourceName = !resourceDirName.empty() ? resourceDirName : resourceName;

        if (!fs->Exists(resourcePath) && !cache->Exists(actualResourceName))
            continue;

        auto it = assets_.find(actualResourceName);
        if (it != assets_.end())
            return it->second;

        if (!autoCreate)
            return nullptr;

        SharedPtr<Asset> asset(context_->CreateObject<Asset>());
        asset->SetName(actualResourceName);
        asset->virtual_ = !fs->Exists(resourcePath);
        asset->resourcePath_ = resourcePath;    // TODO: Clean this up!
        asset->Load();
        assert(asset->GetName() == actualResourceName);
        assets_[actualResourceName] = asset;
        return asset.Get();
    }

    return nullptr;
}

Flavor* Pipeline::AddFlavor(const ea::string& name)
{
    auto it = ea::find(flavors_.begin(), flavors_.end(), name, [](const Flavor* flavor, const ea::string& name) {
        return flavor->GetName() == name;
    });
    if (it != flavors_.end())
        return {};

    SharedPtr<Flavor> flavor(new Flavor(context_));
    flavor->SetName(name);
    flavors_.push_back(flavor);

    using namespace EditorFlavorAdded;
    VariantMap& args = GetEventDataMap();
    args[P_FLAVOR] = flavor;
    SendEvent(E_EDITORFLAVORADDED);

    SortFlavors();
    return flavor;
}

bool Pipeline::RemoveFlavor(const ea::string& name)
{
    if (name == Flavor::DEFAULT)
        return false;

    auto it = ea::find(flavors_.begin(), flavors_.end(), name, [](const Flavor* flavor, const ea::string& name) {
        return flavor->GetName() == name;
    });
    if (it == flavors_.end())
        return false;

    Flavor* flavor = *it;

    using namespace EditorFlavorRemoved;
    VariantMap& args = GetEventDataMap();
    args[P_FLAVOR] = flavor;
    SendEvent(E_EDITORFLAVORREMOVED);

    flavors_.erase(it);
    return true;
}

bool Pipeline::RenameFlavor(const ea::string& oldName, const ea::string& newName)
{
    if (oldName == Flavor::DEFAULT || newName == Flavor::DEFAULT)
        return false;

    auto it = ea::find(flavors_.begin(), flavors_.end(), oldName, [](const Flavor* flavor, const ea::string& name) {
        return flavor->GetName() == name;
    });
    if (it == flavors_.end())
        return false;

    Flavor* flavor = *it;
    flavor->SetName(newName);
    SortFlavors();

    using namespace EditorFlavorRenamed;
    VariantMap& args = GetEventDataMap();
    args[P_FLAVOR] = flavor;
    args[P_OLDNAME] = oldName;
    args[P_NEWNAME] = newName;
    SendEvent(E_EDITORFLAVORRENAMED);

    return true;
}

SharedPtr<WorkItem> Pipeline::ScheduleImport(Asset* asset, Flavor* flavor, PipelineBuildFlags flags)
{
    assert(asset != nullptr);
    if (asset->importing_)
        return {};

    if (flavor == nullptr)
        flavor = GetDefaultFlavor();

    if (flags & PipelineBuildFlag::SKIP_UP_TO_DATE && !asset->IsOutOfDate(flavor))
        return {};

    asset->AddRef();
    asset->importing_ = true;
    return context_->GetSubsystem<WorkQueue>()->AddWorkItem([this, asset, flavor, flags](unsigned /*threadIndex*/)
    {
        if (ExecuteImport(asset, flavor, flags))
        {
            MutexLock lock(mutex_);
            dirtyAssets_.push_back(SharedPtr(asset));
        }
        asset->importing_ = false;
        asset->ReleaseRef();            // TODO: Chance to free asset on non-main thread.
    }, 0);                              // Lowest possible priority.
}

bool Pipeline::ExecuteImport(Asset* asset, Flavor* flavor, PipelineBuildFlags flags)
{
    bool importedAnything = false;
    auto* project = GetSubsystem<Project>();

    ea::string outputPath = project->GetCachePath();
    if (!flavor->IsDefault())
        outputPath += AddTrailingSlash(flavor->GetName());

    for (AssetImporter* importer : asset->importers_[SharedPtr(flavor)])
    {
        // Skip optional importers (importing default flavor when editor is running most likely)
        if (!(flags & PipelineBuildFlag::EXECUTE_OPTIONAL) && (importer->GetFlags() & AssetImporterFlag::IsOptional))
            continue;

        if (!importer->Accepts(asset->GetResourcePath()))
            continue;

        if (importer->Execute(asset, outputPath))
        {
            logger_.Info("{} imported 'res://{}'.", importer->GetTypeName(), asset->GetName());

            importedAnything = true;
            for (const ea::string& byproduct : importer->GetByproducts())
            {
                if (Asset* byproductAsset = GetAsset(byproduct))
                    ExecuteImport(byproductAsset, flavor, flags);
            }
        }
    }

    return importedAnything;
}

void Pipeline::BuildCache(Flavor* flavor, PipelineBuildFlags flags)
{
    auto* project = GetSubsystem<Project>();
    auto* fs = context_->GetSubsystem<FileSystem>();

    if (flavor == nullptr)
        flavor = GetDefaultFlavor();

    StringVector results;
    fs->ScanDir(results, project->GetResourcePath(), "*.*", SCAN_FILES, true);

    for (const ea::string& resourceName : results)
    {
        if (resourceName.ends_with(".asset"))
            continue;

        if (Asset* asset = GetAsset(resourceName))
            ScheduleImport(asset, flavor, flags);
    }
}

void Pipeline::WaitForCompletion() const
{
    context_->GetSubsystem<WorkQueue>()->Complete(0);
}

void Pipeline::CreatePaksAsync(Flavor* flavor)
{
    pendingPackageFlavor_.push_back(SharedPtr(flavor));
    /*
     * Packaging 010
     *
     * Each project has one or more flavors. "default" flavor is present in all projects and is special. Package of "default" flavor
     * contains common assets required by all builds of the project.
     *
     * If asset is not imported by the engine, but used in it's raw form - it will always be included in Resources-default.pak
     *
     * If asset is imported by the engine and has no custom importer settings or if custom settings are set to "default" flavor - byproducts
     * of import procedure will always be included in Resources-default.pak while source asset will not be included in any pak.
     *
     * If asset is imported by the engine and has custom importer settings for flavors other than "default" - each flavor will execute asset
     * import and include byproducts in Resources-flavor.pak. If flavor has no custom settings - settings of default flavor will be used.
     * Source asset will not be included in any paks. No byproducts will be included in Resources-default.pak.
     *
     * Final product must always include Resources-default.pak and any additional paks. For example mobile games may ship
     * Resources-default.pak + Resources-android.pak for android builds as well as Resources-default.pak + Resources-ios.pak for iOS builds.
     * A game meant for desktop computers may ship Resources-default.pak + Resources-low.pak + Resources-medium.pak + Resources-high.pak
     * where low/medium/high flavors have asset import configurations for different asset quality. User would load resources from one of
     * these paks depending on game quality settings.
     *
     */
}

void Pipeline::OnUpdate(StringHash, VariantMap&)
{
    if (packager_.NotNull())
    {
        ui::OpenPopup("Packaging Files");
        if (ui::BeginPopupModal(packagerModalTitle_.c_str(), nullptr,
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_Popup))
        {
            ui::ProgressBar(packager_->GetProgress());
            ui::SetCursorPosX(ui::GetCursorPosX() + 200);
            ui::EndPopup();
        }

        if (packager_->IsCompleted())
        {
            packager_ = nullptr;
        }
    }
    else if (!pendingPackageFlavor_.empty())
    {
        auto* fs = context_->GetSubsystem<FileSystem>();
        auto* project = GetSubsystem<Project>();

        Flavor* flavor = pendingPackageFlavor_.front();
        ea::string packageFile = Format("{}/Resources-{}.pak", project->GetProjectPath(), flavor->GetName());
        packagerModalTitle_ = "Packaging " + GetFileNameAndExtension(packageFile);
        pendingPackageFlavor_.pop_front();

        packager_ = new Packager(context_);
        if (!packager_->OpenPackage(packageFile, flavor))
            return;

        StringVector results;
        fs->ScanDir(results, project->GetResourcePath(), "*.*", SCAN_FILES, true);

        for (const ea::string& resourceName : results)
        {
            if (resourceName.ends_with(".asset"))
                continue;

            bool hasCustomFlavorSettings = HasFlavorSettings(resourceName);
            // Accept default flavor with no custom settings or non-default flavor with custom settings.
            if (flavor->IsDefault() == hasCustomFlavorSettings)
                continue;

            if (Asset* asset = GetAsset(resourceName))
            {
                ScheduleImport(asset, flavor, PipelineBuildFlag::EXECUTE_OPTIONAL | PipelineBuildFlag::SKIP_UP_TO_DATE);
                packager_->AddAsset(asset);
            }
        }
        packager_->Start();
    }
}

bool Pipeline::HasFlavorSettings(const ea::string& resourceName)
{
    StringVector parts = resourceName.split('/');
    bool isDir = resourceName.ends_with("/");

    while (!parts.empty())
    {
        ea::string checkName;
        for (const ea::string& part : parts)
        {
            checkName += part;
            checkName += "/";
        }
        if (!isDir)
        {
            checkName = checkName.substr(0, checkName.length() - 1);    // Strip /
            isDir = true;                                               // next iteration will be dir
        }
        parts.pop_back();

        if (Asset* asset = GetAsset(checkName))
        {
            for (const auto& flavors : asset->GetImporters())
            {
                if (flavors.first->IsDefault())
                    continue;

                for (const auto& importer : flavors.second)
                {
                    if (importer->IsModified())
                        return true;
                }
            }
        }
    }

    return false;
}

bool Pipeline::Serialize(Archive& archive)
{
    if (auto block = archive.OpenUnorderedBlock("pipeline"))
    {
        if (auto block = archive.OpenSequentialBlock("flavors"))
        {
            for (unsigned i = 0, num = archive.IsInput() ? block.GetSizeHint() : flavors_.size(); i < num; i++)
            {
                if (auto block = archive.OpenUnorderedBlock("flavor"))
                {
                    // TODO: This sucks. Flavor should serialize itself. Flavor also has many similarities to ApplicationSettings. Can they share some serialization code?
                    Flavor* flavor = nullptr;
                    ea::string flavorName;
                    StringVector flavorPlatforms;
                    if (!archive.IsInput())
                    {
                        flavor = flavors_[i];
                        flavorName = flavor->GetName();
                        flavorPlatforms = flavor->GetPlatforms();
                    }
                    if (!SerializeValue(archive, "name", flavorName))
                        return false;

                    // Fine to not exist.
                    SerializeValue(archive, "platforms", flavorPlatforms);

                    if (archive.IsInput())
                    {
                        flavor = AddFlavor(flavorName);
                        flavor->GetPlatforms() = flavorPlatforms;
                    }

                    ea::map<ea::string, Variant>& parameters = flavor->GetEngineParameters();
                    if (auto block = archive.OpenMapBlock("settings", parameters.size()))
                    {
                        if (archive.IsInput())
                        {
                            for (unsigned j = 0; j < block.GetSizeHint(); ++j)
                            {
                                ea::string key;
                                if (!archive.SerializeKey(key))
                                    return false;
                                if (!SerializeValue(archive, "value", parameters[key]))
                                    return false;
                            }
                        }
                        else
                        {
                            for (auto& pair : parameters)
                            {
                                if (!archive.SerializeKey(const_cast<ea::string&>(pair.first)))
                                    return false;
                                if (!SerializeValue(archive, "value", pair.second))
                                    return false;
                            }
                        }
                    }
                }
            }
        }
    }

    // Add default flavor if:
    // * This is a new proejct and no flavors were loaded from project file.
    // * User modified project file and renamed default flavor to something else.
    if (archive.IsInput() && (flavors_.empty() || GetDefaultFlavor()->GetName() != Flavor::DEFAULT))
        AddFlavor(Flavor::DEFAULT);

    return true;
}

void Pipeline::SortFlavors()
{
    if (flavors_.size() < 2)
        return;
    auto it = ea::find(flavors_.begin(), flavors_.end(), Flavor::DEFAULT, [](const Flavor* flavor, const ea::string& name) {
        return flavor->GetName() == name;
    });
    if (it != flavors_.end())
        ea::swap(*it, *flavors_.begin());                 // default first
    ea::quick_sort(flavors_.begin() + 1, flavors_.end()); // sort the rest
}

Flavor* Pipeline::GetFlavor(const ea::string& name) const
{
    auto it = ea::find(flavors_.begin(), flavors_.end(), name, [](const Flavor* flavor, const ea::string& name) {
        return flavor->GetName() == name;
    });
    if (it == flavors_.end())
        return nullptr;
    return *it;
}

void Pipeline::OnImporterModified(StringHash, VariantMap& args)
{
    using namespace EditorImporterAttributeModified;
    auto* asset = static_cast<Asset*>(args[P_ASSET].GetPtr());
    auto* importer = static_cast<AssetImporter*>(args[P_IMPORTER].GetPtr());

    if (asset->IsMetaAsset())
        // Meta-assets (directories) are not imported.
        return;

    if (!importer->GetFlavor()->IsImportedByDefault())
        return;

    if (!importer->IsOutOfDate())
        return;

    ScheduleImport(asset, importer->GetFlavor());
}

bool Pipeline::CookSettings() const
{
    auto* project = GetSubsystem<Project>();
    ApplicationSettings settings(context_);
    settings.defaultScene_ = project->GetDefaultSceneName();

    for (Flavor* flavor : GetFlavors())
    {
        settings.platforms_ = flavor->GetPlatforms();
        settings.engineParameters_.clear();
        if (!flavor->IsDefault())
        {
            // All flavors inherit default flavor settings
            auto* defaultFlavor = GetDefaultFlavor();
            settings.engineParameters_.insert(defaultFlavor->GetEngineParameters().begin(), defaultFlavor->GetEngineParameters().end());
        }
        // And then override these any of default settings with ones from the flavor itself
        for (const auto& pair : flavor->GetEngineParameters())
            settings.engineParameters_[pair.first] = pair.second;

#if URHO3D_PLUGINS
        settings.plugins_.clear();
        for (Plugin* plugin : project->GetPlugins()->GetPlugins())
        {
            if (!plugin->IsManagedManually())
                continue;

            if (plugin->IsPrivate())
                continue;

            settings.plugins_.push_back(plugin->GetName());
        }
#endif

        JSONFile file(context_);
        JSONOutputArchive archive(&file);
        if (!settings.Serialize(archive))
            return false;
        context_->GetSubsystem<FileSystem>()->CreateDirsRecursive(flavor->GetCachePath());
        file.SaveFile(flavor->GetCachePath() + "Settings.json");
    }
    return true;
}

bool Pipeline::CookCacheInfo() const
{
    for (Flavor* flavor : GetFlavors())
    {
        ea::unordered_map<ea::string, ea::string> mapping;
        for (const ea::pair<ea::string, SharedPtr<Asset>> assetPair : assets_)
        {
            Asset* asset = assetPair.second;

            for (AssetImporter* importer : asset->GetImporters(flavor))
            {
                if (!(importer->GetFlags() & AssetImporterFlag::IsRemapped))
                    continue;

                if (importer->GetByproducts().empty())
                    continue;

                if (importer->GetByproducts().size() > 1)
                {
                    logger_.Warning("res://{} importer {} has more than one byproduct and can not be remapped.", asset->GetName(),
                        importer->GetTypeName());
                    continue;
                }

                const ea::string& remapCandidate = importer->GetByproducts().front();
                if (mapping.contains(asset->name_))
                {
                    logger_.Warning("res://{} has a remapping candidate res://{}, but previous res://{} remapping is used.",
                        asset->GetName(), remapCandidate, mapping[asset->name_]);
                    continue;
                }

                mapping[asset->name_] = remapCandidate;
            }
        }
        JSONFile file(context_);
        JSONOutputArchive archive(&file);
        if (!SerializeStringMap(archive, "cacheInfo", "map", mapping))
            return false;

        context_->GetSubsystem<FileSystem>()->CreateDirsRecursive(flavor->GetCachePath());
        file.SaveFile(Format("{}CacheInfo.json", flavor->GetCachePath(), flavor->GetName()));
    }
    return true;
}

static const VariantType variantTypes[] = {
    VAR_BOOL,
    VAR_INT,
    VAR_INT64,
    VAR_FLOAT,
    VAR_DOUBLE,
    VAR_COLOR,
    VAR_STRING,
};

static const char* variantNames[] = {
    "Bool",
    "Int",
    "Int64",
    "Float",
    "Double",
    "Color",
    "String",
};

static const char* predefinedNames[] = {
    "Select Option Name",
    "Enter Custom",
    EP_AUTOLOAD_PATHS.c_str(),
    EP_BORDERLESS.c_str(),
    EP_DUMP_SHADERS.c_str(),
    EP_FLUSH_GPU.c_str(),
    EP_FORCE_GL2.c_str(),
    EP_FRAME_LIMITER.c_str(),
    EP_FULL_SCREEN.c_str(),
    EP_HEADLESS.c_str(),
    EP_HIGH_DPI.c_str(),
    EP_LOG_LEVEL.c_str(),
    EP_LOG_NAME.c_str(),
    EP_LOG_QUIET.c_str(),
    EP_LOW_QUALITY_SHADOWS.c_str(),
    EP_MATERIAL_QUALITY.c_str(),
    EP_MONITOR.c_str(),
    EP_MULTI_SAMPLE.c_str(),
    EP_ORGANIZATION_NAME.c_str(),
    EP_APPLICATION_NAME.c_str(),
    EP_ORIENTATIONS.c_str(),
    EP_PACKAGE_CACHE_DIR.c_str(),
    EP_RENDER_PATH.c_str(),
    EP_REFRESH_RATE.c_str(),
    EP_RESOURCE_PACKAGES.c_str(),
    EP_RESOURCE_PATHS.c_str(),
    EP_RESOURCE_PREFIX_PATHS.c_str(),
    EP_SHADER_CACHE_DIR.c_str(),
    EP_SHADOWS.c_str(),
    EP_SOUND.c_str(),
    EP_SOUND_BUFFER.c_str(),
    EP_SOUND_INTERPOLATION.c_str(),
    EP_SOUND_MIX_RATE.c_str(),
    EP_SOUND_STEREO.c_str(),
    EP_TEXTURE_ANISOTROPY.c_str(),
    EP_TEXTURE_FILTER_MODE.c_str(),
    EP_TEXTURE_QUALITY.c_str(),
    EP_TOUCH_EMULATION.c_str(),
    EP_TRIPLE_BUFFER.c_str(),
    EP_VSYNC.c_str(),
    EP_WINDOW_HEIGHT.c_str(),
    EP_WINDOW_ICON.c_str(),
    EP_WINDOW_POSITION_X.c_str(),
    EP_WINDOW_POSITION_Y.c_str(),
    EP_WINDOW_RESIZABLE.c_str(),
    EP_WINDOW_MAXIMIZE.c_str(),
    EP_WINDOW_TITLE.c_str(),
    EP_WINDOW_WIDTH.c_str(),
    EP_WORKER_THREADS.c_str(),
    EP_ENGINE_CLI_PARAMETERS.c_str(),
    EP_ENGINE_AUTO_LOAD_SCRIPTS.c_str(),
};

static VariantType predefinedTypes[] = {
    VAR_NONE,   // Select Option Name
    VAR_NONE,   // Enter Custom
    VAR_STRING, // EP_AUTOLOAD_PATHS
    VAR_BOOL,   // EP_BORDERLESS
    VAR_BOOL,   // EP_DUMP_SHADERS
    VAR_BOOL,   // EP_FLUSH_GPU
    VAR_BOOL,   // EP_FORCE_GL2
    VAR_BOOL,   // EP_FRAME_LIMITER
    VAR_BOOL,   // EP_FULL_SCREEN
    VAR_BOOL,   // EP_HEADLESS
    VAR_BOOL,   // EP_HIGH_DPI
    VAR_INT,    // EP_LOG_LEVEL
    VAR_STRING, // EP_LOG_NAME
    VAR_BOOL,   // EP_LOG_QUIET
    VAR_BOOL,   // EP_LOW_QUALITY_SHADOWS
    VAR_INT,    // EP_MATERIAL_QUALITY
    VAR_INT,    // EP_MONITOR
    VAR_INT,    // EP_MULTI_SAMPLE
    VAR_STRING, // EP_ORGANIZATION_NAME
    VAR_STRING, // EP_APPLICATION_NAME
    VAR_STRING, // EP_ORIENTATIONS
    VAR_STRING, // EP_PACKAGE_CACHE_DIR
    VAR_STRING, // EP_RENDER_PATH
    VAR_INT,    // EP_REFRESH_RATE
    VAR_STRING, // EP_RESOURCE_PACKAGES
    VAR_STRING, // EP_RESOURCE_PATHS
    VAR_STRING, // EP_RESOURCE_PREFIX_PATHS
    VAR_STRING, // EP_SHADER_CACHE_DIR
    VAR_BOOL,   // EP_SHADOWS
    VAR_BOOL,   // EP_SOUND
    VAR_INT,    // EP_SOUND_BUFFER
    VAR_BOOL,   // EP_SOUND_INTERPOLATION
    VAR_INT,    // EP_SOUND_MIX_RATE
    VAR_BOOL,   // EP_SOUND_STEREO
    VAR_INT,    // EP_TEXTURE_ANISOTROPY
    VAR_INT,    // EP_TEXTURE_FILTER_MODE
    VAR_INT,    // EP_TEXTURE_QUALITY
    VAR_BOOL,   // EP_TOUCH_EMULATION
    VAR_BOOL,   // EP_TRIPLE_BUFFER
    VAR_BOOL,   // EP_VSYNC
    VAR_INT,    // EP_WINDOW_HEIGHT
    VAR_STRING, // EP_WINDOW_ICON
    VAR_INT,    // EP_WINDOW_POSITION_X
    VAR_INT,    // EP_WINDOW_POSITION_Y
    VAR_BOOL,   // EP_WINDOW_RESIZABLE
    VAR_BOOL,   // EP_WINDOW_MAXIMIZE
    VAR_STRING, // EP_WINDOW_TITLE
    VAR_INT,    // EP_WINDOW_WIDTH
    VAR_INT,    // EP_WORKER_THREADS
    VAR_BOOL,   // EP_ENGINE_CLI_PARAMETERS
    VAR_BOOL,   // EP_ENGINE_AUTO_LOAD_SCRIPTS
};

static_assert(URHO3D_ARRAYSIZE(predefinedNames) == URHO3D_ARRAYSIZE(predefinedNames), "Sizes must match.");

void Pipeline::RenderSettingsUI()
{
    if (!ui::BeginTabItem("Pipeline"))
        return;

    const auto& style = ui::GetStyle();

    // Add new flavor
    ea::string& newFlavorName = *ui::GetUIState<ea::string>();
    bool canAdd = newFlavorName != Flavor::DEFAULT && !newFlavorName.empty() && GetFlavor(newFlavorName) == nullptr;
    if (!canAdd)
        ui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
    bool addNew = ui::InputText("Flavor Name", &newFlavorName, ImGuiInputTextFlags_EnterReturnsTrue);
    ui::SameLine();
    addNew |= ui::ToolbarButton(ICON_FA_PLUS " Add New");
    if (addNew && canAdd)
        AddFlavor(newFlavorName);
    if (!canAdd)
        ui::PopStyleColor();

    // Flavor tabs
    if (ui::BeginTabBar("Flavors", ImGuiTabBarFlags_AutoSelectNewTabs))
    {
        for (Flavor* flavor : GetFlavors())
        {
            ui::PushID(flavor);
            ea::string& editBuffer = *ui::GetUIState<ea::string>(flavor->GetName());
            bool isOpen = true;
            if (ui::BeginTabItem(flavor->GetName().c_str(), &isOpen, flavor->IsDefault() ? ImGuiTabItemFlags_NoCloseButton | ImGuiTabItemFlags_NoCloseWithMiddleMouseButton : 0))
            {
                bool canRename = editBuffer != Flavor::DEFAULT && !editBuffer.empty() && GetFlavor(editBuffer) == nullptr;
                if (flavor->IsDefault() || !canRename)
                    ui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);

                bool save = ui::InputText("Flavor Name", &editBuffer, ImGuiInputTextFlags_EnterReturnsTrue);
                ui::SameLine();
                save |= ui::ToolbarButton(ICON_FA_CHECK);
                ui::SetHelpTooltip("Rename flavor", KEY_UNKNOWN);
                if (save && canRename && !flavor->IsDefault())
                    RenameFlavor(flavor->GetName(), editBuffer);

                if (flavor->IsDefault() || !canRename)
                    ui::PopStyleColor();

                ui::Separator();

                static const ea::string platforms[] = {"Windows", "Linux", "Android", "iOS", "tvOS", "macOS", "Web"};
                ea::string platformPreview;
                for (const ea::string& platform : flavor->GetPlatforms())
                {
                    platformPreview += platform;
                    platformPreview += ", ";
                }
                if (platformPreview.empty())
                    platformPreview = "Any";
                else
                    platformPreview = platformPreview.substr(0, platformPreview.length() - 2);

                if (ui::BeginCombo("Available on platforms", platformPreview.c_str()))
                {
                    for (const ea::string& platform : platforms)
                    {
                        bool enabled = flavor->GetPlatforms().contains(platform);
                        if (ui::Checkbox(platform.c_str(), &enabled))
                        {
                            if (enabled)
                                flavor->GetPlatforms().push_back(platform);
                            else
                                flavor->GetPlatforms().erase_first(platform);
                        }
                    }
                    ImGui::EndCombo();
                }

                ui::Separator();
                ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
                ui::TextUnformatted("Engine Settings:");
                ui::PushID("Engine Settings");
                struct NewEntryState
                {
                    /// Custom name of the new parameter.
                    ea::string customName_;
                    /// Custom type of the new parameter.
                    int customType_ = 0;
                    /// Index of predefined engine parameter.
                    int predefinedItem_ = 0;
                };

                auto* state = ui::GetUIState<NewEntryState>();
                ea::map<ea::string, Variant>& settings = flavor->GetEngineParameters();
                for (auto it = settings.begin(); it != settings.end();)
                {
                    const ea::string& settingName = it->first;
                    ui::IdScope idScope(settingName.c_str());
                    Variant& value = it->second;
                    float startPos = ui::GetCursorPosX();
                    ui::TextUnformatted(settingName.c_str());
                    ui::SameLine();
                    ui::SetCursorPosX(startPos + 180 + ui::GetStyle().ItemSpacing.x);
                    UI_ITEMWIDTH(100)
                        RenderAttribute("", value);
                    ui::SameLine();
                    ui::SetCursorPosX(startPos + 280 + ui::GetStyle().ItemSpacing.x);
                    if (ui::Button(ICON_FA_TRASH))
                        it = settings.erase(it);
                    else
                        ++it;
                }

                UI_ITEMWIDTH(280)
                    ui::Combo("###Selector", &state->predefinedItem_, predefinedNames, URHO3D_ARRAYSIZE(predefinedNames));

                ui::SameLine();

                const char* cantSubmitHelpText = nullptr;
                if (state->predefinedItem_ == 0)
                    cantSubmitHelpText = "Parameter is not selected.";
                else if (state->predefinedItem_ == 1)
                {
                    if (state->customName_.empty())
                        cantSubmitHelpText = "Custom name can not be empty.";
                    else if (settings.find(state->customName_.c_str()) != settings.end())
                        cantSubmitHelpText = "Parameter with same name is already added.";
                }
                else if (state->predefinedItem_ > 1 && settings.find(predefinedNames[state->predefinedItem_]) !=
                                                      settings.end())
                    cantSubmitHelpText = "Parameter with same name is already added.";

                ui::PushStyleColor(ImGuiCol_Text, ui::GetStyle().Colors[cantSubmitHelpText == nullptr ? ImGuiCol_Text : ImGuiCol_TextDisabled]);
                if (ui::Button(ICON_FA_CHECK) && cantSubmitHelpText == nullptr)
                {
                    if (state->predefinedItem_ == 1)
                        settings.insert({state->customName_.c_str(), Variant{variantTypes[state->customType_]}});
                    else
                        settings.insert({predefinedNames[state->predefinedItem_], Variant{predefinedTypes[state->predefinedItem_]}});
                    state->customName_.clear();
                    state->customType_ = 0;
                }
                ui::PopStyleColor();
                if (cantSubmitHelpText)
                    ui::SetHelpTooltip(cantSubmitHelpText, KEY_UNKNOWN);

                if (state->predefinedItem_ == 1)
                {
                    UI_ITEMWIDTH(180)
                        ui::InputText("###Key", &state->customName_);

                    // Custom entry type selector
                    ui::SameLine();
                    UI_ITEMWIDTH(100 - style.ItemSpacing.x)
                        ui::Combo("###Type", &state->customType_, variantNames, SDL_arraysize(variantTypes));
                }
                ui::PopID();    // Engine Settings

                ui::EndTabItem();
            }
            if (!isOpen && !flavor->IsDefault())
                flavorPendingRemoval_ = flavor;

            if (!flavorPendingRemoval_.Expired())
                ui::OpenPopup("Remove Flavor?");

            if (ui::BeginPopupModal("Remove Flavor?"))
            {
                ui::Text("You are about to remove '%s' flavor.", flavorPendingRemoval_->GetName().c_str());
                ui::TextUnformatted("All asset settings of this flavor will be removed permanently.");
                ui::TextUnformatted(ICON_FA_EXCLAMATION_TRIANGLE " This action can not be undone! " ICON_FA_EXCLAMATION_TRIANGLE);
                ui::NewLine();

                if (ui::Button(ICON_FA_TRASH " Remove"))
                {
                    RemoveFlavor(flavorPendingRemoval_->GetName());
                    flavorPendingRemoval_ = nullptr;
                    ui::CloseCurrentPopup();
                }
                ui::SameLine();
                if (ui::Button(ICON_FA_TIMES " Cancel"))
                {
                    flavorPendingRemoval_ = nullptr;
                    ui::CloseCurrentPopup();
                }

                ui::EndPopup();
            }

            ui::PopID();    // flavor.c_str()
        }

        ui::EndTabBar();
    }

    ui::EndTabItem();
}

}
