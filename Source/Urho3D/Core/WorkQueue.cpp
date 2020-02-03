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

#include "../Core/CoreEvents.h"
#include "../Core/ProcessUtils.h"
#include "../Core/Profiler.h"
#include "../Core/Thread.h"
#include "../Core/Timer.h"
#include "../Core/WorkQueue.h"
#include "../IO/Log.h"

namespace Urho3D
{

/// Worker thread managed by the work queue.
class WorkerThread : public Thread, public RefCounted
{
public:
    /// Construct.
    WorkerThread(WorkQueue* owner, unsigned index) :
        owner_(owner),
        index_(index)
    {
    }

    /// Process work items until stopped.
    void ThreadFunction() override
    {
        URHO3D_PROFILE_THREAD(Format("WorkerThread {}", (uint64_t)GetCurrentThreadID()).c_str());
        // Init FPU state first
        InitFPU();
        owner_->ProcessItems(index_);
    }

    /// Return thread index.
    unsigned GetIndex() const { return index_; }

private:
    /// Work queue.
    WorkQueue* owner_;
    /// Thread index.
    unsigned index_;
};

WorkQueue::WorkQueue(Context* context) :
    Object(context),
    shutDown_(false),
    pausing_(false),
    paused_(false),
    completing_(false),
    tolerance_(10),
    lastSize_(0),
    maxNonThreadedWorkMs_(5)
{
    SubscribeToEvent(E_BEGINFRAME, URHO3D_HANDLER(WorkQueue, HandleBeginFrame));
}

WorkQueue::~WorkQueue()
{
    // Stop the worker threads. First make sure they are not waiting for work items
    shutDown_ = true;
    Resume();

    for (unsigned i = 0; i < threads_.size(); ++i)
        threads_[i]->Stop();
}

void WorkQueue::CreateThreads(unsigned numThreads)
{
#ifdef URHO3D_THREADING
    // Other subsystems may initialize themselves according to the number of threads.
    // Therefore allow creating the threads only once, after which the amount is fixed
    if (!threads_.empty())
        return;

    // Start threads in paused mode
    Pause();

    for (unsigned i = 0; i < numThreads; ++i)
    {
        SharedPtr<WorkerThread> thread(new WorkerThread(this, i + 1));
        thread->SetName(Format("Worker {}", i + 1));
        thread->Run();
        threads_.push_back(thread);
    }
#else
    URHO3D_LOGERROR("Can not create worker threads as threading is disabled");
#endif
}

SharedPtr<WorkItem> WorkQueue::GetFreeItem()
{
    if (!poolItems_.empty())
    {
        SharedPtr<WorkItem> item = poolItems_.front();
        poolItems_.pop_front();
        return item;
    }
    else
    {
        // No usable items found, create a new one set it as pooled and return it.
        SharedPtr<WorkItem> item(new WorkItem());
        item->pooled_ = true;
        return item;
    }
}

void WorkQueue::AddWorkItem(const SharedPtr<WorkItem>& item)
{
    if (!item)
    {
        URHO3D_LOGERROR("Null work item submitted to the work queue");
        return;
    }

    // Check for duplicate items.
    assert(ea::find(workItems_.begin(), workItems_.end(), item) == workItems_.end());

    // Push to the main thread list to keep item alive
    // Clear completed flag in case item is reused
    workItems_.push_back(item);
    item->completed_ = false;

    // Make sure worker threads' list is safe to modify
    if (threads_.size() && !paused_)
        queueMutex_.Acquire();

    // Find position for new item
    if (queue_.empty())
        queue_.push_back(item.Get());
    else
    {
        bool inserted = false;

        for (auto i = queue_.begin(); i != queue_.end(); ++i)
        {
            if ((*i)->priority_ <= item->priority_)
            {
                queue_.insert(i, item.Get());
                inserted = true;
                break;
            }
        }

        if (!inserted)
            queue_.push_back(item.Get());
    }

    if (threads_.size())
    {
        queueMutex_.Release();
        paused_ = false;
    }
}

SharedPtr<WorkItem> WorkQueue::AddWorkItem(std::function<void(unsigned threadIndex)> workFunction, unsigned priority)
{
    SharedPtr<WorkItem> item = GetFreeItem();
    item->workLambda_ = std::move(workFunction);
    item->workFunction_ = [](const WorkItem* item, unsigned threadIndex) { item->workLambda_(threadIndex); };
    item->priority_ = priority;
    AddWorkItem(item);
    return item;
}

bool WorkQueue::RemoveWorkItem(SharedPtr<WorkItem> item)
{
    if (!item)
        return false;

    MutexLock lock(queueMutex_);

    // Can only remove successfully if the item was not yet taken by threads for execution
    auto i = ea::find(queue_.begin(), queue_.end(), item.Get());
    if (i != queue_.end())
    {
        auto j = ea::find(workItems_.begin(), workItems_.end(), item);
        if (j != workItems_.end())
        {
            queue_.erase(i);
            ReturnToPool(item);
            workItems_.erase(j);
            return true;
        }
    }

    return false;
}

unsigned WorkQueue::RemoveWorkItems(const ea::vector<SharedPtr<WorkItem> >& items)
{
    MutexLock lock(queueMutex_);
    unsigned removed = 0;

    for (auto i = items.begin(); i != items.end(); ++i)
    {
        auto j = ea::find(queue_.begin(), queue_.end(), i->Get());
        if (j != queue_.end())
        {
            auto k = ea::find(workItems_.begin(), workItems_.end(), *i);
            if (k != workItems_.end())
            {
                queue_.erase(j);
                ReturnToPool(*k);
                workItems_.erase(k);
                ++removed;
            }
        }
    }

    return removed;
}

void WorkQueue::Pause()
{
    if (!paused_)
    {
        pausing_ = true;

        queueMutex_.Acquire();
        paused_ = true;

        pausing_ = false;
    }
}

void WorkQueue::Resume()
{
    if (paused_)
    {
        queueMutex_.Release();
        paused_ = false;
    }
}


void WorkQueue::Complete(unsigned priority)
{
    completing_ = true;

    if (threads_.size())
    {
        Resume();

        // Take work items also in the main thread until queue empty or no high-priority items anymore
        while (!queue_.empty())
        {
            queueMutex_.Acquire();
            if (!queue_.empty() && queue_.front()->priority_ >= priority)
            {
                WorkItem* item = queue_.front();
                queue_.pop_front();
                queueMutex_.Release();
                item->workFunction_(item, 0);
                item->completed_ = true;
            }
            else
            {
                queueMutex_.Release();
                break;
            }
        }

        // Wait for threaded work to complete
        while (!IsCompleted(priority))
        {
        }

        // If no work at all remaining, pause worker threads by leaving the mutex locked
        if (queue_.empty())
            Pause();
    }
    else
    {
        // No worker threads: ensure all high-priority items are completed in the main thread
        while (!queue_.empty() && queue_.front()->priority_ >= priority)
        {
            WorkItem* item = queue_.front();
            queue_.pop_front();
            item->workFunction_(item, 0);
            item->completed_ = true;
        }
    }

    PurgeCompleted(priority);
    completing_ = false;
}

unsigned WorkQueue::GetNumIncomplete(unsigned priority) const
{
    unsigned incomplete = 0;
    for (const auto& workItem : workItems_)
    {
        if (workItem->priority_ >= priority && !workItem->completed_)
            ++incomplete;
    }

    return incomplete;
}

bool WorkQueue::IsCompleted(unsigned priority) const
{
    for (const auto & workItem : workItems_)
    {
        if (workItem->priority_ >= priority && !workItem->completed_)
            return false;
    }

    return true;
}

void WorkQueue::ProcessItems(unsigned threadIndex)
{
    bool wasActive = false;

    for (;;)
    {
        if (shutDown_)
            return;

        if (pausing_ && !wasActive)
            Time::Sleep(0);
        else
        {
            queueMutex_.Acquire();
            if (!queue_.empty())
            {
                wasActive = true;

                WorkItem* item = queue_.front();
                queue_.pop_front();
                queueMutex_.Release();
                item->workFunction_(item, threadIndex);
                item->completed_ = true;
            }
            else
            {
                wasActive = false;

                queueMutex_.Release();
                Time::Sleep(0);
            }
        }
    }
}

void WorkQueue::PurgeCompleted(unsigned priority)
{
    // Purge completed work items and send completion events. Do not signal items lower than priority threshold,
    // as those may be user submitted and lead to eg. scene manipulation that could happen in the middle of the
    // render update, which is not allowed
    for (auto i = workItems_.begin(); i != workItems_.end();)
    {
        if ((*i)->completed_ && (*i)->priority_ >= priority)
        {
            if ((*i)->sendEvent_)
            {
                using namespace WorkItemCompleted;

                VariantMap& eventData = GetEventDataMap();
                eventData[P_ITEM] = i->Get();
                SendEvent(E_WORKITEMCOMPLETED, eventData);
            }

            ReturnToPool(*i);
            i = workItems_.erase(i);
        }
        else
            ++i;
    }
}

void WorkQueue::PurgePool()
{
    unsigned currentSize = poolItems_.size();
    int difference = lastSize_ - currentSize;

    // Difference tolerance, should be fairly significant to reduce the pool size.
    for (unsigned i = 0; !poolItems_.empty() && difference > tolerance_ && i < (unsigned)difference; i++)
        poolItems_.pop_front();

    lastSize_ = currentSize;
}

void WorkQueue::ReturnToPool(SharedPtr<WorkItem>& item)
{
    // Check if this was a pooled item and set it to usable
    if (item->pooled_)
    {
        // Reset the values to their defaults. This should
        // be safe to do here as the completed event has
        // already been handled and this is part of the
        // internal pool.
        item->start_ = nullptr;
        item->end_ = nullptr;
        item->aux_ = nullptr;
        item->workFunction_ = nullptr;
        item->priority_ = M_MAX_UNSIGNED;
        item->sendEvent_ = false;
        item->completed_ = false;

        poolItems_.push_back(item);
    }
}

void WorkQueue::HandleBeginFrame(StringHash eventType, VariantMap& eventData)
{
    // If no worker threads, complete low-priority work here
    if (threads_.empty() && !queue_.empty())
    {
        URHO3D_PROFILE("CompleteWorkNonthreaded");

        HiresTimer timer;

        while (!queue_.empty() && timer.GetUSec(false) < maxNonThreadedWorkMs_ * 1000LL)
        {
            WorkItem* item = queue_.front();
            queue_.pop_front();
            item->workFunction_(item, 0);
            item->completed_ = true;
        }
    }

    // Complete and signal items down to the lowest priority
    PurgeCompleted(0);
    PurgePool();
}

}
