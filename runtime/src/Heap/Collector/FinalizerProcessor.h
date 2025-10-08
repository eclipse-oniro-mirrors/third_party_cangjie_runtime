// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#ifndef MRT_FINALIZER_PROCESSOR_H
#define MRT_FINALIZER_PROCESSOR_H

#include <climits>
#include <condition_variable>
#include <list>
#include <mutex>

#include "Base/Panic.h"
#include "Common/PageAllocator.h"
#include "Common/TypeDef.h"
#include "Heap/Collector/Collector.h"

namespace MapleRuntime {
template<typename T>
using ManagedList = std::list<T, StdContainerAllocator<T, FINALIZER_PROCESSOR>>;

class FinalizerProcessor {
public:
    FinalizerProcessor();
    ~FinalizerProcessor() = default;

    // mainly for resurrection.
    U32 VisitFinalizers(const RootVisitor& visitor)
    {
        U32 count = 0;
        std::lock_guard<std::mutex> l(listLock);
        for (BaseObject*& obj : finalizers) {
            visitor(reinterpret_cast<ObjectRef&>(obj));
            ++count;
        }
        return count;
    }

    // for : *finalizers* are not proper gc roots.
    void VisitGCRoots(const RootVisitor& visitor)
    {
        std::lock_guard<std::mutex> l(listLock);
        for (BaseObject*& obj : finalizables) {
            visitor(reinterpret_cast<ObjectRef&>(obj));
        }
        for (BaseObject*& obj : workingFinalizables) {
            visitor(reinterpret_cast<ObjectRef&>(obj));
        }
    }

    // mainly for fixing old pointers
    void VisitRawPointers(const RootVisitor& visitor)
    {
        std::lock_guard<std::mutex> l(listLock);
        for (BaseObject*& obj : finalizables) {
            visitor(reinterpret_cast<ObjectRef&>(obj));
        }
        for (BaseObject*& obj : workingFinalizables) {
            visitor(reinterpret_cast<ObjectRef&>(obj));
        }
        for (BaseObject*& obj : finalizers) {
            visitor(reinterpret_cast<ObjectRef&>(obj));
        }
    }

    // notify for finalizer processing loop, invoked after GC
    void Notify();
    // wait started flag set, call after create finalizerProcessor thread
    void WaitStarted();

    void Start();
    void Stop();
    void Run();
    void Init();
    void Fini();
    void WaitStop();

    void EnqueueFinalizables(const std::function<bool(BaseObject*)>& finalizable, U32 countLimit = UINT_MAX);
    void RegisterFinalizer(BaseObject* obj);
    bool IsRunning() const { return running; }
    uint32_t GetTid() const { return tid; }

    Mutator* GetMutator() const { return fpMutator; }

    void NotifyToReclaimGarbage() { shouldReclaimHeapGarbage.store(true); }
    void NotifyToFeedAllocBuffers()
    {
        shouldFeedHungryBuffers.store(true, std::memory_order_release);
        Notify();
    }

private:
    void NotifyStarted();
    void Wait(U32 timeoutMilliSeconds);
    void ProcessFinalizables();
    void ProcessFinalizableList();
    void ReclaimHeapGarbage();
    void FeedHungryBuffers();

    std::mutex wakeLock;
    std::condition_variable wakeCondition; // notify finalizer processing continue

    std::mutex startedLock;
    std::condition_variable startedCondition; // notify finalizerProcessor thread is started
    volatile bool started;

    volatile bool running; // Initially false and set true after finalizerProcessor thread start, set false when stop

    U32 iterationWaitTime;

    // finalization
    std::mutex listLock;                 // lock for finalizers & finalizables & workingFinalizables
    ManagedList<BaseObject*> finalizers; // created finalizer record, accessed by mutator & GC

    // a dead finalizer is moved into finalizable by GC, then run finalize method by FP thread
    ManagedList<BaseObject*> finalizables;

    ManagedList<BaseObject*> workingFinalizables; // FP working list, swap from finalizables

    std::atomic<bool> hasFinalizableJob;
    std::atomic<bool> shouldReclaimHeapGarbage;
    std::atomic<bool> shouldFeedHungryBuffers;
#if defined(MRT_DEBUG) && (MRT_DEBUG == 1)
    // stats
    void LogAfterProcess();
#endif
    uint64_t timeProcessorBegin;
    uint64_t timeProcessUsed;
    uint64_t timeCurrentProcessBegin;
    uint32_t tid = 0;
    pthread_t threadHandle = 0; // thread handle to thread
    Mutator* fpMutator = nullptr;
};
} // namespace MapleRuntime
#endif // MRT_FINALIZER_PROCESSOR_H
