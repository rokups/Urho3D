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

#include "../Graphics/LightBaker.h"

#include "../Core/Context.h"
#include "../Core/CoreEvents.h"
#include "../Core/StopToken.h"
#include "../Core/Timer.h"
#include "../Graphics/GlobalIllumination.h"
#include "../Graphics/Octree.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/Skybox.h"
#include "../Graphics/Zone.h"
#include "../IO/Log.h"
#include "../Scene/Scene.h"

#if URHO3D_GLOW
#include "../Glow/IncrementalLightBaker.h"
#endif

namespace Urho3D
{

namespace
{

static const char* qualityNames[] =
{
    "Custom",
    "Low",
    "Medium",
    "High",
    nullptr
};

}

extern const char* SUBSYSTEM_CATEGORY;

/// State of async light baker task.
struct LightBaker::TaskData
{
    /// Caller.
    WeakPtr<LightBaker> weakSelf_;
    /// Stop token.
    StopToken stopToken_;
    /// Timer to measure total time.
    Timer timer_;
#if URHO3D_GLOW
    /// Scene collector.
    DefaultBakedSceneCollector sceneCollector_;
    /// Memory cache
    BakedLightMemoryCache cache_;
    /// Baker.
    IncrementalLightBaker baker_;
#endif
};

LightBaker::LightBaker(Context* context) :
    Component(context)
{
    SubscribeToEvent(E_UPDATE, [this](StringHash eventType, VariantMap& eventData)
    {
        Update();
    });
}

LightBaker::~LightBaker()
{
    if (state_ != InternalState::NotStarted)
    {
        taskData_->stopToken_.Stop();
        task_.wait();
    }
}

void LightBaker::RegisterObject(Context* context)
{
    static const LightBakingSettings defaultSettings;
    context->RegisterFactory<LightBaker>(SUBSYSTEM_CATEGORY);

    auto getBake = [](const ClassName& self, Urho3D::Variant& value) { value = self.state_ != InternalState::NotStarted; };
    auto setBake = [](ClassName& self, const Urho3D::Variant& value) { if (value.GetBool()) self.BakeAsync(); };
    URHO3D_CUSTOM_ACCESSOR_ATTRIBUTE("Bake!", getBake, setBake, bool, false, AM_EDIT);

    URHO3D_ATTRIBUTE("Output Directory", ea::string, settings_.incremental_.outputDirectory_, "", AM_DEFAULT);
    URHO3D_ATTRIBUTE("Lightmap Size", unsigned, settings_.charting_.lightmapSize_, defaultSettings.charting_.lightmapSize_, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Texel Density", float, settings_.charting_.texelDensity_, defaultSettings.charting_.texelDensity_, AM_DEFAULT);
    URHO3D_ENUM_ACCESSOR_ATTRIBUTE("Quality", GetQuality, SetQuality, LightBakingQuality, qualityNames, LightBakingQuality::Custom, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Direct Samples (Lightmap)", unsigned, settings_.directChartTracing_.maxSamples_, defaultSettings.directChartTracing_.maxSamples_, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Direct Samples (Light Probes)", unsigned, settings_.directProbesTracing_.maxSamples_, defaultSettings.directProbesTracing_.maxSamples_, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Indirect Bounces", unsigned, settings_.indirectChartTracing_.maxBounces_, defaultSettings.indirectChartTracing_.maxBounces_, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Indirect Samples (Texture)", unsigned, settings_.indirectChartTracing_.maxSamples_, defaultSettings.indirectChartTracing_.maxSamples_, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Indirect Samples (Light Probes)", unsigned, settings_.indirectProbesTracing_.maxSamples_, defaultSettings.indirectProbesTracing_.maxSamples_, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Filter Radius (Direct)", unsigned, settings_.directFilter_.kernelRadius_, defaultSettings.directFilter_.kernelRadius_, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Filter Radius (Indirect)", unsigned, settings_.indirectFilter_.kernelRadius_, defaultSettings.indirectFilter_.kernelRadius_, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Chunk Size", Vector3, settings_.incremental_.chunkSize_, defaultSettings.incremental_.chunkSize_, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Chunk Indirect Padding", float, settings_.incremental_.indirectPadding_, defaultSettings.incremental_.indirectPadding_, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Chunk Shadow Distance", float, settings_.incremental_.directionalLightShadowDistance_, defaultSettings.incremental_.directionalLightShadowDistance_, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Stitch Iterations", unsigned, settings_.stitching_.numIterations_, defaultSettings.stitching_.numIterations_, AM_DEFAULT);
}

void LightBaker::SetQuality(LightBakingQuality quality)
{
    quality_ = quality;
    switch (quality)
    {
    case LightBakingQuality::Low:
        settings_.directChartTracing_.maxSamples_ = 10;
        settings_.directProbesTracing_.maxSamples_ = 32;
        settings_.indirectChartTracing_.maxSamples_ = 10;
        settings_.indirectProbesTracing_.maxSamples_ = 64;
        break;
    case LightBakingQuality::Medium:
        settings_.directChartTracing_.maxSamples_ = 32;
        settings_.directProbesTracing_.maxSamples_ = 32;
        settings_.indirectChartTracing_.maxSamples_ = 64;
        settings_.indirectProbesTracing_.maxSamples_ = 256;
        break;
    case LightBakingQuality::High:
        settings_.directChartTracing_.maxSamples_ = 32;
        settings_.directProbesTracing_.maxSamples_ = 32;
        settings_.indirectChartTracing_.maxSamples_ = 256;
        settings_.indirectProbesTracing_.maxSamples_ = 256;
        break;
    case LightBakingQuality::Custom:
    default:
        break;
    }
}

void LightBaker::Bake()
{
    if (state_ == InternalState::NotStarted)
    {
        state_ = InternalState::ScheduledSync;
        Update();
    }
}

void LightBaker::BakeAsync()
{
    if (state_ == InternalState::NotStarted)
        state_ = InternalState::ScheduledAsync;
}

bool LightBaker::UpdateSettings()
{
    Scene* scene = GetScene();
    auto octree = scene->GetComponent<Octree>();
    auto gi = scene->GetComponent<GlobalIllumination>();
    Zone* zone = octree->GetBackgroundZone();

    if (!gi)
    {
        URHO3D_LOGERROR("GlobalIllumination scene system is required to bake light");
        return false;
    }

    // Fill settings
    settings_.indirectProbesTracing_.maxBounces_ = settings_.indirectChartTracing_.maxBounces_;

    const unsigned numTasks = settings_.charting_.lightmapSize_;
    settings_.geometryBufferPreprocessing_.numTasks_ = numTasks;
    settings_.emissionTracing_.numTasks_ = numTasks;
    settings_.directChartTracing_.numTasks_ = numTasks;
    settings_.directProbesTracing_.numTasks_ = numTasks;
    settings_.indirectChartTracing_.numTasks_ = numTasks;
    settings_.indirectProbesTracing_.numTasks_ = numTasks;

    settings_.properties_.emissionBrightness_ = gi->GetEmissionBrightness();
    return true;
}

void LightBaker::Update()
{
    // Start baking
    if (state_ == InternalState::ScheduledSync || state_ == InternalState::ScheduledAsync)
    {
#if URHO3D_GLOW
        if (!UpdateSettings())
        {
            return;
        }

        auto taskData = ea::make_shared<TaskData>();
        taskData->weakSelf_ = this;
        if (!taskData->baker_.Initialize(settings_, GetScene(), &taskData->sceneCollector_, &taskData->cache_))
        {
            URHO3D_LOGERROR("Cannot initialize light baking");
            state_ = InternalState::NotStarted;
            return;
        }

        // Do all the work with Scene here
        taskData->baker_.ProcessScene();

        // Bake now or schedule task
        if (state_ == InternalState::ScheduledSync)
        {
            taskData->baker_.Bake(taskData->stopToken_);

            state_ = InternalState::CommitPending;
            taskData_ = taskData;
            // Fall through to the next if
        }
        else
        {
            const auto taskFunction = [taskData]()
            {
                taskData->baker_.Bake(taskData->stopToken_);

                // Self is never destroyed before the task is finished
                taskData->weakSelf_->state_ = InternalState::CommitPending;
            };

            task_ = std::async(taskFunction);

            // Don't expect any results now, so return
            state_ = InternalState::InProgress;
            taskData_ = taskData;
            return;
        }
#else
        // Cannot start baking, return
        URHO3D_LOGERROR("Enable URHO3D_GLOW in build options");
        state_ = InternalState::NotStarted;
        return;
#endif
    }

    // Commit changes
    if (state_ == InternalState::CommitPending)
    {
        // If was async task, close future
        if (task_.valid())
            task_.get();

#if URHO3D_GLOW
        taskData_->baker_.CommitScene();
#endif

        // Compile light probes
        auto gi = GetScene()->GetComponent<GlobalIllumination>();
        gi->CompileLightProbes();

        // Log overall time
        const unsigned totalMSec = taskData_->timer_.GetMSec(true);
        URHO3D_LOGINFO("Light baking is finished in {} seconds", totalMSec / 1000);

        // Reset
        state_ = InternalState::NotStarted;
        taskData_ = nullptr;
    }
}

}
