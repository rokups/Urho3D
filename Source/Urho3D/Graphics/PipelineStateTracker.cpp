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

void PipelineStateSubscription::Reset(PipelineStateTracker* sender, PipelineStateTracker* subscriber)
{
    // Add new reference first
    if (subscriber && sender)
        sender->AddSubscriberReference(subscriber);

    // Remove old reference
    if (subscriber_ && sender_)
        sender_->RemoveSubscriberReference(subscriber_);

    // Relink pointers
    subscriber_ = subscriber;
    sender_ = sender;
}

PipelineStateSubscription PipelineStateTracker::CreateDependency(PipelineStateTracker* sender)
{
    return { sender, this };
}

void PipelineStateTracker::AddSubscriberReference(PipelineStateTracker* subscriber)
{
    if (!subscriber)
        return;
    auto iter = FindSubscriberIter(subscriber);
    if (iter != subscribers_.end())
        ++iter->second;
    else
        subscribers_.emplace_back(subscriber, 1u);
    subscriber->MarkPipelineStateHashDirty();
}

void PipelineStateTracker::RemoveSubscriberReference(PipelineStateTracker* subscriber)
{
    if (!subscriber)
        return;
    auto iter = FindSubscriberIter(subscriber);
    assert(iter != subscribers_.end());
    --iter->second;
    if (iter->second == 0)
        subscribers_.erase(iter);
    subscriber->MarkPipelineStateHashDirty();
}

void PipelineStateTracker::MarkPipelineStateHashDirty()
{
    const unsigned oldHash = pipelineStateHash_.exchange(0, std::memory_order_relaxed);
    if (oldHash != 0)
    {
        for (const auto& item : subscribers_)
            item.first->MarkPipelineStateHashDirty();
    }
}

PipelineStateTracker::DependantVector::iterator PipelineStateTracker::FindSubscriberIter(PipelineStateTracker* subscriber)
{
    const auto pred = [&](const ea::pair<PipelineStateTracker*, unsigned>& elem) { return elem.first == subscriber; };
    return ea::find_if(subscribers_.begin(), subscribers_.end(), pred);
}

}
