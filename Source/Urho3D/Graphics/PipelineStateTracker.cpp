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

#include "../Graphics/PipelineStateTracker.h"

namespace Urho3D
{

void PipelineStateDependency::Reset(PipelineStateTracker* dependency, PipelineStateTracker* dependant)
{
    // Add new reference first
    if (dependant && dependency)
        dependency->AddObserverReference(dependant);

    // Remove old reference
    if (dependant_ && dependency_)
        dependency_->RemoveObserverReference(dependant_);

    // Relink pointers
    dependant_ = dependant;
    dependency_ = dependency;
}

PipelineStateDependency PipelineStateTracker::CreateDependency(PipelineStateTracker* dependency)
{
    return { dependency, this };
}

void PipelineStateTracker::AddObserverReference(PipelineStateTracker* dependant)
{
    if (!dependant)
        return;
    auto iter = FindDependantTrackerIter(dependant);
    if (iter != dependantTrackers_.end())
        ++iter->second;
    else
        dependantTrackers_.emplace_back(dependant, 1u);
    dependant->MarkPipelineStateHashDirty();
}

void PipelineStateTracker::RemoveObserverReference(PipelineStateTracker* dependant)
{
    if (!dependant)
        return;
    auto iter = FindDependantTrackerIter(dependant);
    assert(iter != dependantTrackers_.end());
    --iter->second;
    if (iter->second == 0)
        dependantTrackers_.erase(iter);
    dependant->MarkPipelineStateHashDirty();
}

void PipelineStateTracker::MarkPipelineStateHashDirty()
{
    const unsigned oldHash = pipelineStateHash_.exchange(0, std::memory_order_relaxed);
    if (oldHash != 0)
    {
        for (const auto& item : dependantTrackers_)
            item.first->MarkPipelineStateHashDirty();
    }
}

PipelineStateTracker::DependantVector::iterator PipelineStateTracker::FindDependantTrackerIter(PipelineStateTracker* dependant)
{
    const auto pred = [&](const ea::pair<PipelineStateTracker*, unsigned>& elem) { return elem.first == dependant; };
    return ea::find_if(dependantTrackers_.begin(), dependantTrackers_.end(), pred);
}

}
