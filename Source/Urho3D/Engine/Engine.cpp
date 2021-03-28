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

#include "../Audio/Audio.h"
#include "../Core/Context.h"
#include "../Core/CoreEvents.h"
#include "../Core/Profiler.h"
#include "../Core/ProcessUtils.h"
#include "../Core/Thread.h"
#include "../Core/WorkQueue.h"
#ifdef URHO3D_SYSTEMUI
#include "../SystemUI/SystemUI.h"
#include "../SystemUI/Console.h"
#include "../SystemUI/DebugHud.h"
#endif
#include "../Engine/Engine.h"
#include "../Engine/EngineDefs.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Renderer.h"
#include "../Input/Input.h"
#include "../IO/FileSystem.h"
#include "../IO/Log.h"
#include "../IO/PackageFile.h"
#ifdef URHO3D_GLOW
#include "../Glow/StaticModelForLightmap.h"
#endif
#ifdef URHO3D_IK
#include "../IK/IK.h"
#endif
#ifdef URHO3D_NAVIGATION
#include "../Navigation/NavigationMesh.h"
#endif
#ifdef URHO3D_NETWORK
#include "../Network/Network.h"
#endif
#ifdef URHO3D_PHYSICS
#include "../Physics/PhysicsWorld.h"
#include "../Physics/RaycastVehicle.h"
#endif
#include "../Resource/ResourceCache.h"
#include "../Resource/Localization.h"
#ifndef URHO3D_D3D9
#include "../RenderPipeline/RenderPipeline.h"
#endif
#include "../Scene/Scene.h"
#include "../Scene/SceneEvents.h"
#include "../UI/UI.h"
#ifdef URHO3D_RMLUI
#include "../RmlUI/RmlUI.h"
#endif
#ifdef URHO3D_URHO2D
#include "../Urho2D/Urho2D.h"
#endif
#include "../Engine/EngineEvents.h"

#if defined(__EMSCRIPTEN__) && defined(URHO3D_TESTING)
#include <emscripten/emscripten.h>
#endif

#include <Urho3D/Core/CommandLine.h>

#include "../DebugNew.h"


#if defined(_MSC_VER) && defined(_DEBUG)
// From dbgint.h
#define nNoMansLandSize 4

typedef struct _CrtMemBlockHeader
{
    struct _CrtMemBlockHeader* pBlockHeaderNext;
    struct _CrtMemBlockHeader* pBlockHeaderPrev;
    char* szFileName;
    int nLine;
    size_t nDataSize;
    int nBlockUse;
    long lRequest;
    unsigned char gap[nNoMansLandSize];
} _CrtMemBlockHeader;
#endif

namespace Urho3D
{

extern const char* logLevelNames[];

Engine::Engine(Context* context) :
    Object(context),
    timeStep_(0.0f),
    timeStepSmoothing_(2),
    minFps_(10),
#if defined(IOS) || defined(TVOS) || defined(__ANDROID__) || defined(__arm__) || defined(__aarch64__)
    maxFps_(60),
    maxInactiveFps_(10),
    pauseMinimized_(true),
#else
    maxFps_(200),
    maxInactiveFps_(60),
    pauseMinimized_(false),
#endif
#ifdef URHO3D_TESTING
    timeOut_(0),
#endif
    autoExit_(true),
    initialized_(false),
    exiting_(false),
    headless_(false),
    audioPaused_(false)
{
    // Register self as a subsystem
    context_->RegisterSubsystem(this);

    // Create subsystems which do not depend on engine initialization or startup parameters
    context_->RegisterSubsystem(new Time(context_));
    context_->RegisterSubsystem(new WorkQueue(context_));
    context_->RegisterSubsystem(new FileSystem(context_));
#ifdef URHO3D_LOGGING
    context_->RegisterSubsystem(new Log(context_));
#endif
    context_->RegisterSubsystem(new ResourceCache(context_));
    context_->RegisterSubsystem(new Localization(context_));
#ifdef URHO3D_NETWORK
    context_->RegisterSubsystem(new Network(context_));
#endif
    // Register UI library object factories before creation of subsystem. This is not done inside subsystem because
    // there may exist multiple instances of UI.
    RegisterUILibrary(context_);
    context_->RegisterSubsystem(new UI(context_));
#ifdef URHO3D_RMLUI
    RegisterRmlUILibrary(context_);
    context_->RegisterSubsystem(new RmlUI(context_));
#endif
    // Register object factories for libraries which are not automatically registered along with subsystem creation
    RegisterSceneLibrary(context_);

#ifdef URHO3D_GLOW
    // Light baker needs only one class so far, so register it directly.
    // Extract this code into function if you are adding more.
    StaticModelForLightmap::RegisterObject(context_);
#endif

#ifndef URHO3D_D3D9
    // Register render pipeline.
    // Extract this code into function if you are adding more.
    RenderPipeline::RegisterObject(context_);
#endif

#ifdef URHO3D_IK
    RegisterIKLibrary(context_);
#endif

#ifdef URHO3D_PHYSICS
    RegisterPhysicsLibrary(context_);
#endif

#ifdef URHO3D_NAVIGATION
    RegisterNavigationLibrary(context_);
#endif

    SubscribeToEvent(E_EXITREQUESTED, URHO3D_HANDLER(Engine, HandleExitRequested));
    SubscribeToEvent(E_ENDFRAME, URHO3D_HANDLER(Engine, HandleEndFrame));
}

Engine::~Engine() = default;

bool Engine::Initialize(const VariantMap& parameters)
{
    if (initialized_)
        return true;

    URHO3D_PROFILE("InitEngine");

    // Start logging
    auto* log = GetSubsystem<Log>();
    if (log)
    {
        if (HasParameter(parameters, EP_LOG_LEVEL))
            log->SetLevel(static_cast<LogLevel>(GetParameter(parameters, EP_LOG_LEVEL).GetInt()));
        log->SetQuiet(GetParameter(parameters, EP_LOG_QUIET, false).GetBool());
        log->Open(GetParameter(parameters, EP_LOG_NAME, "Urho3D.log").GetString());
    }

    // Set headless mode
    headless_ = GetParameter(parameters, EP_HEADLESS, false).GetBool();

    // Register the rest of the subsystems
    context_->RegisterSubsystem(new Input(context_));
    context_->RegisterSubsystem(new Audio(context_));
    if (!headless_)
    {
        context_->RegisterSubsystem(new Graphics(context_));
        context_->RegisterSubsystem(new Renderer(context_));
    }
    else
    {
        // Register graphics library objects explicitly in headless mode to allow them to work without using actual GPU resources
        RegisterGraphicsLibrary(context_);
    }

#ifdef URHO3D_URHO2D
    // 2D graphics library is dependent on 3D graphics library
    RegisterUrho2DLibrary(context_);
#endif

    // Set maximally accurate low res timer
    GetSubsystem<Time>()->SetTimerPeriod(1);

    // Configure max FPS
    if (GetParameter(parameters, EP_FRAME_LIMITER, true) == false)
        SetMaxFps(0);

    // Set amount of worker threads according to the available physical CPU cores. Using also hyperthreaded cores results in
    // unpredictable extra synchronization overhead. Also reserve one core for the main thread
#ifdef URHO3D_THREADING
    unsigned numThreads = GetParameter(parameters, EP_WORKER_THREADS, true).GetBool() ? GetNumPhysicalCPUs() - 1 : 0;
    if (numThreads)
    {
        GetSubsystem<WorkQueue>()->CreateThreads(numThreads);

        URHO3D_LOGINFOF("Created %u worker thread%s", numThreads, numThreads > 1 ? "s" : "");
    }
#endif

    // Add resource paths
    if (!InitializeResourceCache(parameters, false))
        return false;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* fileSystem = GetSubsystem<FileSystem>();

    // Initialize graphics & audio output
    if (!headless_)
    {
        auto* graphics = GetSubsystem<Graphics>();
        auto* renderer = GetSubsystem<Renderer>();

        if (HasParameter(parameters, EP_EXTERNAL_WINDOW))
            graphics->SetExternalWindow(GetParameter(parameters, EP_EXTERNAL_WINDOW).GetVoidPtr());
        graphics->SetWindowTitle(GetParameter(parameters, EP_WINDOW_TITLE, "Urho3D").GetString());
        graphics->SetWindowIcon(cache->GetResource<Image>(GetParameter(parameters, EP_WINDOW_ICON, EMPTY_STRING).GetString()));
        graphics->SetFlushGPU(GetParameter(parameters, EP_FLUSH_GPU, false).GetBool());
        graphics->SetOrientations(GetParameter(parameters, EP_ORIENTATIONS, "LandscapeLeft LandscapeRight").GetString());
        graphics->SetShaderValidationEnabled(GetParameter(parameters, EP_VALIDATE_SHADERS, false).GetBool());

#ifdef URHO3D_OPENGL
        if (HasParameter(parameters, EP_FORCE_GL2))
            graphics->SetForceGL2(GetParameter(parameters, EP_FORCE_GL2).GetBool());
#endif

        if (!graphics->SetMode(
            GetParameter(parameters, EP_WINDOW_WIDTH, 0).GetInt(),
            GetParameter(parameters, EP_WINDOW_HEIGHT, 0).GetInt(),
            GetParameter(parameters, EP_FULL_SCREEN, true).GetBool(),
            GetParameter(parameters, EP_BORDERLESS, false).GetBool(),
            GetParameter(parameters, EP_WINDOW_RESIZABLE, false).GetBool(),
            GetParameter(parameters, EP_HIGH_DPI, true).GetBool(),
            GetParameter(parameters, EP_VSYNC, false).GetBool(),
            GetParameter(parameters, EP_TRIPLE_BUFFER, false).GetBool(),
            GetParameter(parameters, EP_MULTI_SAMPLE, 1).GetInt(),
            GetParameter(parameters, EP_MONITOR, 0).GetInt(),
            GetParameter(parameters, EP_REFRESH_RATE, 0).GetInt()
        ))
            return false;

        if (HasParameter(parameters, EP_WINDOW_POSITION_X) && HasParameter(parameters, EP_WINDOW_POSITION_Y))
            graphics->SetWindowPosition(GetParameter(parameters, EP_WINDOW_POSITION_X).GetInt(),
                GetParameter(parameters, EP_WINDOW_POSITION_Y).GetInt());

        if (HasParameter(parameters, EP_WINDOW_MAXIMIZE) && GetParameter(parameters, EP_WINDOW_MAXIMIZE).GetBool())
            graphics->Maximize();

        graphics->SetShaderCacheDir(GetParameter(parameters, EP_SHADER_CACHE_DIR, appPreferencesDir_).GetString() + "shadercache/");

        if (HasParameter(parameters, EP_DUMP_SHADERS))
            graphics->BeginDumpShaders(GetParameter(parameters, EP_DUMP_SHADERS, EMPTY_STRING).GetString());
        if (HasParameter(parameters, EP_RENDER_PATH))
            renderer->SetDefaultRenderPath(cache->GetResource<XMLFile>(GetParameter(parameters, EP_RENDER_PATH).GetString()));

        renderer->SetDrawShadows(GetParameter(parameters, EP_SHADOWS, true).GetBool());
        if (renderer->GetDrawShadows() && GetParameter(parameters, EP_LOW_QUALITY_SHADOWS, false).GetBool())
            renderer->SetShadowQuality(SHADOWQUALITY_SIMPLE_16BIT);
        renderer->SetMaterialQuality((MaterialQuality)GetParameter(parameters, EP_MATERIAL_QUALITY, QUALITY_HIGH).GetInt());
        renderer->SetTextureQuality((MaterialQuality)GetParameter(parameters, EP_TEXTURE_QUALITY, QUALITY_HIGH).GetInt());
        renderer->SetTextureFilterMode((TextureFilterMode)GetParameter(parameters, EP_TEXTURE_FILTER_MODE, FILTER_TRILINEAR).GetInt());
        renderer->SetTextureAnisotropy(GetParameter(parameters, EP_TEXTURE_ANISOTROPY, 4).GetInt());

        if (GetParameter(parameters, EP_SOUND, true).GetBool())
        {
            GetSubsystem<Audio>()->SetMode(
                GetParameter(parameters, EP_SOUND_BUFFER, 100).GetInt(),
                GetParameter(parameters, EP_SOUND_MIX_RATE, 44100).GetInt(),
                GetParameter(parameters, EP_SOUND_STEREO, true).GetBool(),
                GetParameter(parameters, EP_SOUND_INTERPOLATION, true).GetBool()
            );
        }
    }

    // Init FPU state of main thread
    InitFPU();

    // Initialize input
    if (HasParameter(parameters, EP_TOUCH_EMULATION))
        GetSubsystem<Input>()->SetTouchEmulation(GetParameter(parameters, EP_TOUCH_EMULATION).GetBool());

    // Initialize network
#ifdef URHO3D_NETWORK
    if (HasParameter(parameters, EP_PACKAGE_CACHE_DIR))
        GetSubsystem<Network>()->SetPackageCacheDir(GetParameter(parameters, EP_PACKAGE_CACHE_DIR).GetString());
#endif

#ifdef URHO3D_TESTING
    if (HasParameter(parameters, EP_TIME_OUT))
        timeOut_ = GetParameter(parameters, EP_TIME_OUT, 0).GetInt() * 1000000LL;
#endif
    if (!headless_)
    {
#ifdef URHO3D_SYSTEMUI
        context_->RegisterSubsystem(new SystemUI(context_,
            GetParameter(parameters, EP_SYSTEMUI_FLAGS, 0).GetUInt()));
#endif
    }
    frameTimer_.Reset();

    URHO3D_LOGINFO("Initialized engine");
    initialized_ = true;
    SendEvent(E_ENGINEINITIALIZED);
    return true;
}

bool Engine::InitializeResourceCache(const VariantMap& parameters, bool removeOld /*= true*/)
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* fileSystem = GetSubsystem<FileSystem>();

    // Initialize app preferences directory
    appPreferencesDir_ = fileSystem->GetAppPreferencesDir(
        GetParameter(parameters, EP_ORGANIZATION_NAME, "urho3d").GetString(),
        GetParameter(parameters, EP_APPLICATION_NAME, "engine").GetString());

    // Remove all resource paths and packages
    if (removeOld)
    {
        ea::vector<ea::string> resourceDirs = cache->GetResourceDirs();
        ea::vector<SharedPtr<PackageFile> > packageFiles = cache->GetPackageFiles();
        for (unsigned i = 0; i < resourceDirs.size(); ++i)
            cache->RemoveResourceDir(resourceDirs[i]);
        for (unsigned i = 0; i < packageFiles.size(); ++i)
            cache->RemovePackageFile(packageFiles[i].Get());
    }

    // Add resource paths
    ea::vector<ea::string> resourcePrefixPaths = GetParameter(parameters, EP_RESOURCE_PREFIX_PATHS,
        EMPTY_STRING).GetString().split(';', true);
    for (unsigned i = 0; i < resourcePrefixPaths.size(); ++i)
        resourcePrefixPaths[i] = AddTrailingSlash(
            IsAbsolutePath(resourcePrefixPaths[i]) ? resourcePrefixPaths[i] : fileSystem->GetProgramDir() + resourcePrefixPaths[i]);
    ea::vector<ea::string> resourcePaths = GetParameter(parameters, EP_RESOURCE_PATHS,
        "Data;CoreData").GetString().split(';');
    ea::vector<ea::string> resourcePackages = GetParameter(parameters, EP_RESOURCE_PACKAGES).GetString().split(';');
    ea::vector<ea::string> autoLoadPaths = GetParameter(parameters, EP_AUTOLOAD_PATHS, "Autoload").GetString().split(';');

    for (unsigned i = 0; i < resourcePaths.size(); ++i)
    {
        // If path is not absolute, prefer to add it as a package if possible
        if (!IsAbsolutePath(resourcePaths[i]))
        {
            unsigned j = 0;
            for (; j < resourcePrefixPaths.size(); ++j)
            {
                ea::string packageName = resourcePrefixPaths[j] + resourcePaths[i] + ".pak";
                if (fileSystem->FileExists(packageName))
                {
                    if (cache->AddPackageFile(packageName))
                        break;
                    else
                        return false;   // The root cause of the error should have already been logged
                }
                ea::string pathName = resourcePrefixPaths[j] + resourcePaths[i];
                if (fileSystem->DirExists(pathName))
                {
                    if (cache->AddResourceDir(pathName))
                        break;
                    else
                        return false;
                }
            }
            if (j == resourcePrefixPaths.size() && !headless_)
            {
                URHO3D_LOGERRORF(
                    "Failed to add resource path '%s', check the documentation on how to set the 'resource prefix path'",
                    resourcePaths[i].c_str());
                return false;
            }
        }
        else
        {
            ea::string pathName = resourcePaths[i];
            if (fileSystem->DirExists(pathName))
                if (!cache->AddResourceDir(pathName))
                    return false;
        }
    }

    // Then add specified packages
    for (unsigned i = 0; i < resourcePackages.size(); ++i)
    {
        unsigned j = 0;
        for (; j < resourcePrefixPaths.size(); ++j)
        {
            ea::string packageName = resourcePrefixPaths[j] + resourcePackages[i];
            if (fileSystem->FileExists(packageName))
            {
                if (cache->AddPackageFile(packageName))
                    break;
                else
                    return false;
            }
        }
        if (j == resourcePrefixPaths.size() && !headless_)
        {
            URHO3D_LOGERRORF(
                "Failed to add resource package '%s', check the documentation on how to set the 'resource prefix path'",
                resourcePackages[i].c_str());
            return false;
        }
    }

    // Add auto load folders. Prioritize these (if exist) before the default folders
    for (unsigned i = 0; i < autoLoadPaths.size(); ++i)
    {
        bool autoLoadPathExist = false;

        for (unsigned j = 0; j < resourcePrefixPaths.size(); ++j)
        {
            ea::string autoLoadPath(autoLoadPaths[i]);
            if (!IsAbsolutePath(autoLoadPath))
                autoLoadPath = resourcePrefixPaths[j] + autoLoadPath;

            if (fileSystem->DirExists(autoLoadPath))
            {
                autoLoadPathExist = true;

                // Add all the subdirs (non-recursive) as resource directory
                ea::vector<ea::string> subdirs;
                fileSystem->ScanDir(subdirs, autoLoadPath, "*", SCAN_DIRS, false);
                for (unsigned y = 0; y < subdirs.size(); ++y)
                {
                    ea::string dir = subdirs[y];
                    if (dir.starts_with("."))
                        continue;

                    ea::string autoResourceDir = AddTrailingSlash(autoLoadPath) + dir;
                    if (!cache->AddResourceDir(autoResourceDir, 0))
                        return false;
                }

                // Add all the found package files (non-recursive)
                ea::vector<ea::string> paks;
                fileSystem->ScanDir(paks, autoLoadPath, "*.pak", SCAN_FILES, false);
                for (unsigned y = 0; y < paks.size(); ++y)
                {
                    ea::string pak = paks[y];
                    if (pak.starts_with("."))
                        continue;

                    ea::string autoPackageName = autoLoadPath + "/" + pak;
                    if (!cache->AddPackageFile(autoPackageName, 0))
                        return false;
                }
            }
        }

        // The following debug message is confusing when user is not aware of the autoload feature
        // Especially because the autoload feature is enabled by default without user intervention
        // The following extra conditional check below is to suppress unnecessary debug log entry under such default situation
        // The cleaner approach is to not enable the autoload by default, i.e. do not use 'Autoload' as default value for 'AutoloadPaths' engine parameter
        // However, doing so will break the existing applications that rely on this
        if (!autoLoadPathExist && (autoLoadPaths.size() > 1 || autoLoadPaths[0] != "Autoload"))
            URHO3D_LOGDEBUGF(
                "Skipped autoload path '%s' as it does not exist, check the documentation on how to set the 'resource prefix path'",
                autoLoadPaths[i].c_str());
    }

    return true;
}

void Engine::RunFrame()
{
    URHO3D_PROFILE("RunFrame");
    {
        assert(initialized_);

        // If not headless, and the graphics subsystem no longer has a window open, assume we should exit
        if (!headless_ && !GetSubsystem<Graphics>()->IsInitialized())
            exiting_ = true;

        if (exiting_)
            return;
    }

    // Note: there is a minimal performance cost to looking up subsystems (uses a hashmap); if they would be looked up several
    // times per frame it would be better to cache the pointers
    auto* time = GetSubsystem<Time>();
    auto* input = GetSubsystem<Input>();
    auto* audio = GetSubsystem<Audio>();

    {
        URHO3D_PROFILE("DoFrame");
        time->BeginFrame(timeStep_);

        // If pause when minimized -mode is in use, stop updates and audio as necessary
        if (pauseMinimized_ && input->IsMinimized())
        {
            if (audio->IsPlaying())
            {
                audio->Stop();
                audioPaused_ = true;
            }
        }
        else
        {
            // Only unpause when it was paused by the engine
            if (audioPaused_)
            {
                audio->Play();
                audioPaused_ = false;
            }

            Update();
        }

        Render();
    }
    ApplyFrameLimit();

    time->EndFrame();

    // Mark a frame for profiling
    URHO3D_PROFILE_FRAME();
}

Console* Engine::CreateConsole()
{
    if (headless_ || !initialized_)
        return nullptr;

#ifdef URHO3D_SYSTEMUI
    // Return existing console if possible
    auto* console = GetSubsystem<Console>();
    if (!console)
    {
        console = new Console(context_);
        context_->RegisterSubsystem(console);
    }

    return console;
#else
    return nullptr;
#endif
}

DebugHud* Engine::CreateDebugHud()
{
    if (headless_ || !initialized_)
        return nullptr;

#ifdef URHO3D_SYSTEMUI
    // Return existing debug HUD if possible
    auto* debugHud = GetSubsystem<DebugHud>();
    if (!debugHud)
    {
        debugHud = new DebugHud(context_);
        context_->RegisterSubsystem(debugHud);
    }

    return debugHud;
#else
    return nullptr;
#endif
}

void Engine::SetTimeStepSmoothing(int frames)
{
    timeStepSmoothing_ = (unsigned)Clamp(frames, 1, 20);
}

void Engine::SetMinFps(int fps)
{
    minFps_ = (unsigned)Max(fps, 0);
}

void Engine::SetMaxFps(int fps)
{
    maxFps_ = (unsigned)Max(fps, 0);
}

void Engine::SetMaxInactiveFps(int fps)
{
    maxInactiveFps_ = (unsigned)Max(fps, 0);
}

void Engine::SetPauseMinimized(bool enable)
{
    pauseMinimized_ = enable;
}

void Engine::SetAutoExit(bool enable)
{
    // On mobile platforms exit is mandatory if requested by the platform itself and should not be attempted to be disabled
#if defined(__ANDROID__) || defined(IOS) || defined(TVOS)
    enable = true;
#endif
    autoExit_ = enable;
}

void Engine::SetNextTimeStep(float seconds)
{
    timeStep_ = Max(seconds, 0.0f);
}

void Engine::Exit()
{
#if defined(IOS) || defined(TVOS)
    // On iOS/tvOS it's not legal for the application to exit on its own, instead it will be minimized with the home key
#else
    DoExit();
#endif
}

void Engine::DumpProfiler()
{
#ifdef URHO3D_LOGGING
    if (!Thread::IsMainThread())
        return;

    // TODO: Put something here or remove API
#endif
}

void Engine::DumpResources(bool dumpFileName)
{
#ifdef URHO3D_LOGGING
    if (!Thread::IsMainThread())
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    const ea::unordered_map<StringHash, ResourceGroup>& resourceGroups = cache->GetAllResources();
    if (dumpFileName)
    {
        URHO3D_LOGINFO("Used resources:");
        for (auto i = resourceGroups.begin(); i !=
            resourceGroups.end(); ++i)
        {
            const ea::unordered_map<StringHash, SharedPtr<Resource> >& resources = i->second.resources_;
            if (dumpFileName)
            {
                for (auto j = resources.begin(); j !=
                    resources.end(); ++j)
                    URHO3D_LOGINFO(j->second->GetName());
            }
        }
    }
    else
        URHO3D_LOGINFO(cache->PrintMemoryUsage());
#endif
}

void Engine::DumpMemory()
{
#ifdef URHO3D_LOGGING
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtMemState state;
    _CrtMemCheckpoint(&state);
    _CrtMemBlockHeader* block = state.pBlockHeader;
    unsigned total = 0;
    unsigned blocks = 0;

    for (;;)
    {
        if (block && block->pBlockHeaderNext)
            block = block->pBlockHeaderNext;
        else
            break;
    }

    while (block)
    {
        if (block->nBlockUse > 0)
        {
            if (block->szFileName)
                URHO3D_LOGINFO("Block {}: {} bytes, file {} line {}", block->lRequest, block->nDataSize, block->szFileName, block->nLine);
            else
                URHO3D_LOGINFO("Block {}: {} bytes", block->lRequest, block->nDataSize);

            total += block->nDataSize;
            ++blocks;
        }
        block = block->pBlockHeaderPrev;
    }

    URHO3D_LOGINFO("Total allocated memory {} bytes in {} blocks", total, blocks);
#else
    URHO3D_LOGINFO("DumpMemory() supported on MSVC debug mode only");
#endif
#endif
}

void Engine::Update()
{
    URHO3D_PROFILE("Update");

    // Logic update event
    using namespace Update;

    VariantMap& eventData = GetEventDataMap();
    eventData[P_TIMESTEP] = timeStep_;
    SendEvent(E_UPDATE, eventData);

    // Logic post-update event
    SendEvent(E_POSTUPDATE, eventData);

    // Rendering update event
    SendEvent(E_RENDERUPDATE, eventData);

    // Post-render update event
    SendEvent(E_POSTRENDERUPDATE, eventData);
}

void Engine::Render()
{
    if (headless_)
        return;

    URHO3D_PROFILE("Render");

    // If device is lost, BeginFrame will fail and we skip rendering
    auto* graphics = GetSubsystem<Graphics>();
    if (!graphics->BeginFrame())
        return;

    GetSubsystem<Renderer>()->Render();

    // Render UI after scene is rendered, but only do so if user has not rendered it manually
    // anywhere (for example using renderpath or to a texture).
    graphics->ResetRenderTargets();
    if (UI* ui = GetSubsystem<UI>())
    {
        if (!ui->IsRendered() && ui->GetRenderTarget() == nullptr)
            ui->Render();
    }

    graphics->EndFrame();
}

void Engine::ApplyFrameLimit()
{
    if (!initialized_)
        return;

    unsigned maxFps = maxFps_;
    auto* input = GetSubsystem<Input>();
    if (input && !input->HasFocus())
        maxFps = Min(maxInactiveFps_, maxFps);

    long long elapsed = 0;

#ifndef __EMSCRIPTEN__
    // Perform waiting loop if maximum FPS set
#if !defined(IOS) && !defined(TVOS)
    if (maxFps)
#else
    // If on iOS/tvOS and target framerate is 60 or above, just let the animation callback handle frame timing
    // instead of waiting ourselves
    if (maxFps < 60)
#endif
    {
        URHO3D_PROFILE("ApplyFrameLimit");

        long long targetMax = 1000000LL / maxFps;

        for (;;)
        {
            elapsed = frameTimer_.GetUSec(false);
            if (elapsed >= targetMax)
                break;

            // Sleep if 1 ms or more off the frame limiting goal
            if (targetMax - elapsed >= 1000LL)
            {
                auto sleepTime = (unsigned)((targetMax - elapsed) / 1000LL);
                Time::Sleep(sleepTime);
            }
        }
    }
#endif

    elapsed = frameTimer_.GetUSec(true);
#ifdef URHO3D_TESTING
    if (timeOut_ > 0)
    {
        timeOut_ -= elapsed;
        if (timeOut_ <= 0)
            Exit();
    }
#endif

    // If FPS lower than minimum, clamp elapsed time
    if (minFps_)
    {
        long long targetMin = 1000000LL / minFps_;
        if (elapsed > targetMin)
            elapsed = targetMin;
    }

    // Perform timestep smoothing
    timeStep_ = 0.0f;
    lastTimeSteps_.push_back(elapsed / 1000000.0f);
    if (lastTimeSteps_.size() > timeStepSmoothing_)
    {
        // If the smoothing configuration was changed, ensure correct amount of samples
        lastTimeSteps_.erase_at(0, timeStepSmoothing_);
        for (unsigned i = 0; i < lastTimeSteps_.size(); ++i)
            timeStep_ += lastTimeSteps_[i];
        timeStep_ /= lastTimeSteps_.size();
    }
    else
        timeStep_ = lastTimeSteps_.back();
}

#if DESKTOP
void Engine::DefineParameters(CLI::App& commandLine, VariantMap& engineParameters)
{
    auto addFlagInternal = [&](const char* name, const char* description, CLI::callback_t fun) {
        CLI::Option *opt = commandLine.add_option(name, fun, description, false);
        if(opt->get_positional())
            throw CLI::IncorrectConstruction::PositionalFlag(name);
        opt->set_custom_option("", 0);
        return opt;
    };

    auto addFlag = [&](const char* name, const ea::string& param, bool value, const char* description) {
        CLI::callback_t fun = [&engineParameters, param, value](CLI::results_t) {
            engineParameters[param] = value;
            return true;
        };
        return addFlagInternal(name, description, fun);
    };

    auto addOptionPrependString = [&](const char* name, const ea::string& param, const ea::string& value, const char* description) {
        CLI::callback_t fun = [&engineParameters, param, value](CLI::results_t) {
            engineParameters[param] = value + engineParameters[param].GetString();
            return true;
        };
        return addFlagInternal(name, description, fun);
    };

    auto addOptionSetString = [&](const char* name, const ea::string& param, const ea::string& value, const char* description) {
        CLI::callback_t fun = [&engineParameters, param, value](CLI::results_t) {
            engineParameters[param] = value;
            return true;
        };
        return addFlagInternal(name, description, fun);
    };

    auto addOptionString = [&](const char* name, const ea::string& param, const char* description) {
        CLI::callback_t fun = [&engineParameters, param](CLI::results_t res) {
            engineParameters[param] = res[0].c_str();
            return true;
        };
        auto* opt = addFlagInternal(name, description, fun);
        opt->set_custom_option("string");
        return opt;
    };

    auto addOptionInt = [&](const char* name, const ea::string& param, const char* description) {
        CLI::callback_t fun = [&engineParameters, param](CLI::results_t res) {
            int value = 0;
            if (CLI::detail::lexical_cast(res[0], value))
            {
                engineParameters[param] = value;
                return true;
            }
            return false;
        };
        auto* opt = addFlagInternal(name, description, fun);
        opt->set_custom_option("int");
        return opt;
    };

    auto createOptions = [](const char* format, const char* options[]) {
        StringVector items;
        for (unsigned i = 0; options[i]; i++)
            items.push_back(options[i]);
        return ToString(format, ea::string::joined(items, "|").to_lower().replaced('_', '-').c_str());
    };

    addFlag("--headless", EP_HEADLESS, true, "Do not initialize graphics subsystem");
    addFlag("--validate-shaders", EP_VALIDATE_SHADERS, true, "Validate shaders before submitting them to GAPI");
    addFlag("--nolimit", EP_FRAME_LIMITER, false, "Disable frame limiter");
    addFlag("--flushgpu", EP_FLUSH_GPU, true, "Enable GPU flushing");
    addFlag("--gl2", EP_FORCE_GL2, true, "Force OpenGL2");
    addOptionPrependString("--landscape", EP_ORIENTATIONS, "LandscapeLeft LandscapeRight ", "Force landscape orientation");
    addOptionPrependString("--portrait", EP_ORIENTATIONS, "Portrait PortraitUpsideDown ", "Force portrait orientation");
    addFlag("--nosound", EP_SOUND, false, "Disable sound");
    addFlag("--noip", EP_SOUND_INTERPOLATION, false, "Disable sound interpolation");
    addFlag("--mono", EP_SOUND_STEREO, false, "Force mono sound output (default is stereo)");
    auto* optRenderpath = addOptionString("--renderpath", EP_RENDER_PATH, "Use custom renderpath");
    auto* optPrepass = addOptionSetString("--prepass", EP_RENDER_PATH, "RenderPaths/Prepass.xml", "Use prepass renderpath")->excludes(optRenderpath);
    auto* optDeferred = addOptionSetString("--deferred", EP_RENDER_PATH, "RenderPaths/Deferred.xml", "Use deferred renderpath")->excludes(optRenderpath);
    optRenderpath->excludes(optPrepass)->excludes(optDeferred);
    auto* optNoShadows = addFlag("--noshadows", EP_SHADOWS, false, "Disable shadows");
    auto optLowQualityShadows = addFlag("--lqshadows", EP_LOW_QUALITY_SHADOWS, true, "Use low quality shadows")->excludes(optNoShadows);
    optNoShadows->excludes(optLowQualityShadows);
    addFlag("--nothreads", EP_WORKER_THREADS, false, "Disable multithreading");
    addFlag("-v,--vsync", EP_VSYNC, true, "Enable vsync");
    addFlag("-t,--tripple-buffer", EP_TRIPLE_BUFFER, true, "Enable tripple-buffering");
    addFlag("-w,--windowed", EP_FULL_SCREEN, false, "Windowed mode");
    addFlag("-f,--full-screen", EP_FULL_SCREEN, true, "Full screen mode");
    addFlag("--borderless", EP_BORDERLESS, true, "Borderless window mode");
    addFlag("--lowdpi", EP_HIGH_DPI, false, "Disable high-dpi handling");
    addFlag("--highdpi", EP_HIGH_DPI, true, "Enable high-dpi handling");
    addFlag("-s,--resizable", EP_WINDOW_RESIZABLE, true, "Enable window resizing");
    addFlag("-q,--quiet", EP_LOG_QUIET, true, "Disable logging");
    addFlagInternal("-l,--log", "Logging level", [&](CLI::results_t res) {
        unsigned logLevel = GetStringListIndex(ea::string(res[0].c_str()).to_upper().c_str(), logLevelNames, M_MAX_UNSIGNED);
        if (logLevel == M_MAX_UNSIGNED)
            return false;
        engineParameters[EP_LOG_LEVEL] = logLevel;
        return true;
    })->set_custom_option(createOptions("string in {%s}", logLevelNames).c_str());
    addOptionString("--log-file", EP_LOG_NAME, "Log output file");
    addOptionInt("-x,--width", EP_WINDOW_WIDTH, "Window width");
    addOptionInt("-y,--height", EP_WINDOW_HEIGHT, "Window height");
    addOptionInt("--monitor", EP_MONITOR, "Create window on the specified monitor");
    addOptionInt("--hz", EP_REFRESH_RATE, "Use custom refresh rate");
    addOptionInt("-m,--multisample", EP_MULTI_SAMPLE, "Multisampling samples");
    addOptionInt("-b,--sound-buffer", EP_SOUND_BUFFER, "Sound buffer size");
    addOptionInt("-r,--mix-rate", EP_SOUND_MIX_RATE, "Sound mixing rate");
    addOptionString("--pp,--prefix-paths", EP_RESOURCE_PREFIX_PATHS, "Resource prefix paths")->envname("URHO3D_PREFIX_PATH")->set_custom_option("path1;path2;...");
    addOptionString("--pr,--resource-paths", EP_RESOURCE_PATHS, "Resource paths")->set_custom_option("path1;path2;...");
    addOptionString("--pf,--resource-packages", EP_RESOURCE_PACKAGES, "Resource packages")->set_custom_option("path1;path2;...");
    addOptionString("--ap,--autoload-paths", EP_AUTOLOAD_PATHS, "Resource autoload paths")->set_custom_option("path1;path2;...");
    addOptionString("--ds,--dump-shaders", EP_DUMP_SHADERS, "Dump shaders")->set_custom_option("filename");
    addFlagInternal("--mq,--material-quality", "Material quality", [&](CLI::results_t res) {
        unsigned value = 0;
        if (CLI::detail::lexical_cast(res[0], value) && value >= QUALITY_LOW && value <= QUALITY_MAX)
        {
            engineParameters[EP_MATERIAL_QUALITY] = value;
            return true;
        }
        return false;
    })->set_custom_option(ToString("int {%d-%d}", QUALITY_LOW, QUALITY_MAX).c_str());
    addFlagInternal("--tq", "Texture quality", [&](CLI::results_t res) {
        unsigned value = 0;
        if (CLI::detail::lexical_cast(res[0], value) && value >= QUALITY_LOW && value <= QUALITY_MAX)
        {
            engineParameters[EP_TEXTURE_QUALITY] = value;
            return true;
        }
        return false;
    })->set_custom_option(ToString("int {%d-%d}", QUALITY_LOW, QUALITY_MAX).c_str());
    addFlagInternal("--tf", "Texture filter mode", [&](CLI::results_t res) {
        unsigned mode = GetStringListIndex(ea::string(res[0].c_str()).to_upper().replaced('-', '_').c_str(), textureFilterModeNames, M_MAX_UNSIGNED);
        if (mode == M_MAX_UNSIGNED)
            return false;
        engineParameters[EP_TEXTURE_FILTER_MODE] = mode;
        return true;
    })->set_custom_option(createOptions("string in {%s}", textureFilterModeNames).c_str());
    addFlagInternal("--af", "Use anisotropic filtering", [&](CLI::results_t res) {
        int value = 0;
        if (CLI::detail::lexical_cast(res[0], value) && value >= 1)
        {
            engineParameters[EP_TEXTURE_FILTER_MODE] = FILTER_ANISOTROPIC;
            engineParameters[EP_TEXTURE_ANISOTROPY] = value;
            return true;
        }
        return false;
    })->set_custom_option("int");
    addFlag("--touch", EP_TOUCH_EMULATION, true, "Enable touch emulation");
#ifdef URHO3D_TESTING
    addOptionInt("--timeout", EP_TIME_OUT, "Quit application after specified time");
#endif
}
#endif

bool Engine::HasParameter(const VariantMap& parameters, const ea::string& parameter)
{
    StringHash nameHash(parameter);
    return parameters.find(nameHash) != parameters.end();
}

const Variant& Engine::GetParameter(const VariantMap& parameters, const ea::string& parameter, const Variant& defaultValue)
{
    StringHash nameHash(parameter);
    auto i = parameters.find(nameHash);
    return i != parameters.end() ? i->second : defaultValue;
}

void Engine::HandleExitRequested(StringHash eventType, VariantMap& eventData)
{
    exiting_ = true;
}

void Engine::HandleEndFrame(StringHash eventType, VariantMap& eventData)
{
    if (exiting_ && autoExit_)
    {
        // Do not call Exit() here, as it contains mobile platform -specific tests to not exit.
        // If we do receive an exit request from the system on those platforms, we must comply
        DoExit();
    }
}

void Engine::DoExit()
{
    auto* graphics = GetSubsystem<Graphics>();
    if (graphics)
        graphics->Close();

#if defined(__EMSCRIPTEN__) && defined(URHO3D_TESTING)
    emscripten_force_exit(EXIT_SUCCESS);    // Some how this is required to signal emrun to stop
#endif
}

}
