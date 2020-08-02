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

#pragma once

#include "../Core/Mutex.h"
#include "../Core/Object.h"
#include "../Core/ThreadedVector.h"

#include <EASTL/list.h>
#include <EASTL/span.h>
#include <atomic>

namespace Urho3D
{

/// Work item completed event.
URHO3D_EVENT(E_WORKITEMCOMPLETED, WorkItemCompleted)
{
    URHO3D_PARAM(P_ITEM, Item);                        // WorkItem ptr
}

class WorkerThread;

/// Work queue item.
/// @nobind
struct WorkItem : public RefCounted
{
    friend class WorkQueue;

public:
    /// Work function. Called with the work item and thread index (0 = main thread) as parameters.
    void (* workFunction_)(const WorkItem*, unsigned){};
    /// Data start pointer.
    void* start_{};
    /// Data end pointer.
    void* end_{};
    /// Auxiliary data pointer.
    void* aux_{};
    /// Priority. Higher value = will be completed first.
    unsigned priority_{};
    /// Whether to send event on completion.
    bool sendEvent_{};
    /// Completed flag.
    std::atomic<bool> completed_{};

private:
    bool pooled_{};
    /// Work function. Called without any parameters.
    std::function<void(unsigned threadIndex)> workLambda_;
};

/// Work queue subsystem for multithreading.
class URHO3D_API WorkQueue : public Object
{
    URHO3D_OBJECT(WorkQueue, Object);

    friend class WorkerThread;

public:
    /// Construct.
    explicit WorkQueue(Context* context);
    /// Destruct.
    ~WorkQueue() override;

    /// Create worker threads. Can only be called once.
    void CreateThreads(unsigned numThreads);
    /// Get pointer to an usable WorkItem from the item pool. Allocate one if no more free items.
    SharedPtr<WorkItem> GetFreeItem();
    /// Add a work item and resume worker threads.
    void AddWorkItem(const SharedPtr<WorkItem>& item);
    /// Add a work item and resume worker threads.
    SharedPtr<WorkItem> AddWorkItem(std::function<void(unsigned threadIndex)> workFunction, unsigned priority = 0);
    /// Remove a work item before it has started executing. Return true if successfully removed.
    bool RemoveWorkItem(SharedPtr<WorkItem> item);
    /// Remove a number of work items before they have started executing. Return the number of items successfully removed.
    unsigned RemoveWorkItems(const ea::vector<SharedPtr<WorkItem> >& items);
    /// Pause worker threads.
    void Pause();
    /// Resume worker threads.
    void Resume();
    /// Finish all queued work which has at least the specified priority. Main thread will also execute priority work. Pause worker threads if no more work remains.
    void Complete(unsigned priority);

    /// Set the pool telerance before it starts deleting pool items.
    void SetTolerance(int tolerance) { tolerance_ = tolerance; }

    /// Set how many milliseconds maximum per frame to spend on low-priority work, when there are no worker threads.
    void SetNonThreadedWorkMs(int ms) { maxNonThreadedWorkMs_ = Max(ms, 1); }

    /// Return number of worker threads.
    unsigned GetNumThreads() const { return threads_.size(); }

    /// Return number of incomplete tasks with at least the specified priority.
    unsigned GetNumIncomplete(unsigned priority) const;
    /// Return whether all work with at least the specified priority is finished.
    bool IsCompleted(unsigned priority) const;
    /// Return whether the queue is currently completing work in the main thread.
    bool IsCompleting() const { return completing_; }

    /// Return the pool tolerance.
    int GetTolerance() const { return tolerance_; }

    /// Return how many milliseconds maximum to spend on non-threaded low-priority work.
    int GetNonThreadedWorkMs() const { return maxNonThreadedWorkMs_; }

    /// Return current worker thread.
    static unsigned GetWorkerThreadIndex();

private:
    /// Process work items until shut down. Called by the worker threads.
    void ProcessItems(unsigned threadIndex);
    /// Purge completed work items which have at least the specified priority, and send completion events as necessary.
    void PurgeCompleted(unsigned priority);
    /// Purge the pool to reduce allocation where its unneeded.
    void PurgePool();
    /// Return a work item to the pool.
    void ReturnToPool(SharedPtr<WorkItem>& item);
    /// Handle frame start event. Purge completed work from the main thread queue, and perform work if no threads at all.
    void HandleBeginFrame(StringHash eventType, VariantMap& eventData);

    /// Worker threads.
    ea::vector<SharedPtr<WorkerThread> > threads_;
    /// Work item pool for reuse to cut down on allocation. The bool is a flag for item pooling and whether it is available or not.
    ea::list<SharedPtr<WorkItem> > poolItems_;
    /// Work item collection. Accessed only by the main thread.
    ea::list<SharedPtr<WorkItem> > workItems_;
    /// Work item prioritized queue for worker threads. Pointers are guaranteed to be valid (point to workItems).
    ea::list<WorkItem*> queue_;
    /// Worker queue mutex.
    Mutex queueMutex_;
    /// Shutting down flag.
    std::atomic<bool> shutDown_;
    /// Pausing flag. Indicates the worker threads should not contend for the queue mutex.
    std::atomic<bool> pausing_;
    /// Paused flag. Indicates the queue mutex being locked to prevent worker threads using up CPU time.
    bool paused_;
    /// Completing work in the main thread flag.
    bool completing_;
    /// Tolerance for the shared pool before it begins to deallocate.
    int tolerance_;
    /// Last size of the shared pool.
    unsigned lastSize_;
    /// Maximum milliseconds per frame to spend on low-priority work, when there are no worker threads.
    int maxNonThreadedWorkMs_;
};

/// For-each algorithm executed in parallel via WorkQueue over contiguous collection.
/// Callback signature is `void(unsigned threadIndex, unsigned offset, ea::span<T const> elements)`.
template <class T, class U>
void ForEachParallel(WorkQueue* workQueue, unsigned threshold, ea::span<T> collection, const U& callback)
{
    assert(threshold > 0);
    const unsigned numElements = collection.size();
    if (numElements == 0)
        return;

    const unsigned maxThreads = workQueue->GetNumThreads() + 1;
    const unsigned maxTasks = ea::max(1u, ea::min(numElements / threshold, maxThreads));

    const unsigned elementsPerTask = (numElements + maxTasks - 1) / maxTasks;
    for (unsigned taskIndex = 0; taskIndex < maxTasks; ++taskIndex)
    {
        const unsigned fromIndex = ea::min(taskIndex * elementsPerTask, numElements);
        const unsigned toIndex = ea::min((taskIndex + 1) * elementsPerTask, numElements);
        if (fromIndex == toIndex)
            continue;

        const auto range = collection.subspan(fromIndex, toIndex - fromIndex);
        workQueue->AddWorkItem([callback, fromIndex, range](unsigned threadIndex)
        {
            callback(threadIndex, fromIndex, range);
        }, M_MAX_UNSIGNED);
    }
    workQueue->Complete(M_MAX_UNSIGNED);
}

/// For-each algorithm executed in parallel via WorkQueue over vector.
template <class T, class U>
void ForEachParallel(WorkQueue* workQueue, unsigned threshold, const ea::vector<T>& collection, const U& callback)
{
    ForEachParallel(workQueue, threshold, ea::span<const T>(collection), callback);
}

/// For-each algorithm executed in parallel via WorkQueue over ThreadedVector.
template <class T, class U>
void ForEachParallel(WorkQueue* workQueue, unsigned threshold, const ThreadedVector<T>& collection, const U& callback)
{
    assert(threshold > 0);
    const unsigned numElements = collection.Size();
    if (numElements == 0)
        return;

    const unsigned maxThreads = workQueue->GetNumThreads() + 1;
    const unsigned maxTasks = ea::max(1u, ea::min(numElements / threshold, maxThreads));

    const unsigned elementsPerTask = (numElements + maxTasks - 1) / maxTasks;
    for (unsigned taskIndex = 0; taskIndex < maxTasks; ++taskIndex)
    {
        const unsigned fromIndex = ea::min(taskIndex * elementsPerTask, numElements);
        const unsigned toIndex = ea::min((taskIndex + 1) * elementsPerTask, numElements);
        if (fromIndex == toIndex)
            continue;

        workQueue->AddWorkItem([callback, &collection, fromIndex, toIndex](unsigned threadIndex)
        {
            const auto& threadedCollections = collection.GetUnderlyingCollection();
            unsigned baseIndex = 0;
            for (const auto& threadCollection : threadedCollections)
            {
                // Stop if whole range is processed
                if (baseIndex >= toIndex)
                    break;

                // Skip if didn't get to the range yet
                const unsigned numElementsInCollection = threadCollection.size();
                if (baseIndex + numElementsInCollection > fromIndex)
                {
                    // Remap range
                    const unsigned fromSubIndex = ea::max(baseIndex, fromIndex) - baseIndex;
                    const unsigned toSubIndex = ea::min(toIndex - baseIndex, numElementsInCollection);
                    if (fromSubIndex != toSubIndex)
                    {
                        // Invoke callback for desired range
                        const ea::span<const T> elements(&threadCollection[fromSubIndex], toSubIndex - fromSubIndex);
                        callback(threadIndex, baseIndex + fromSubIndex, elements);
                    }
                }

                // Update base index
                baseIndex += threadCollection.size();
            }
        }, M_MAX_UNSIGNED);
    }
    workQueue->Complete(M_MAX_UNSIGNED);
}

}
