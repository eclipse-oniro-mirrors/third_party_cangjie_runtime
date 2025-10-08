// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#ifndef MRT_MUTATOR_H
#define MRT_MUTATOR_H

#include <climits>

#include "Exception/Exception.h"
#include "Heap/Allocator/Allocator.h"
#include "Heap/Collector/GcInfos.h"
#include "LoaderManager.h"
#include "Mutator/ThreadLocal.h"
#include "SatbBuffer.h"
#include "schedule.h"
#ifdef _WIN64
#include "UnwindWin.h"
#endif


namespace MapleRuntime {
extern "C" MRT_EXPORT bool MRT_EnterSaferegion(bool updateUnwindContext);
extern "C" MRT_EXPORT bool MRT_LeaveSaferegion();
extern "C" MRT_EXPORT bool MRT_CheckRuntimeFinished();

class Mutator {
public:
    // flag which indicates the reason why mutator should suspend. flag is set by some external thread.
    enum SuspensionType : uint32_t {
        SUSPENSION_FOR_GC_PHASE = 1,
        SUSPENSION_FOR_SYNC = 2,
        SUSPENSION_FOR_EXIT = 4,
        SUSPENSION_FOR_CPU_PROFILE = 8,
    };

    enum GCPhaseTransitionState : uint32_t {
        NO_TRANSITION,
        NEED_TRANSITION,
        IN_TRANSITION,
        FINISH_TRANSITION,
    };

    enum CpuProfileState : uint32_t {
        NO_CPUPROFILE,
        NEED_CPUPROFILE,
        IN_CPUPROFILING,
        FINISH_CPUPROFILE,
    };

    // Indicate whether mutator is in saferegion
    enum SaferegionState : uint32_t {
        SAFE_REGION_TRUE = 0x17161514,
        SAFE_REGION_FALSE = 0x03020100,
    };

    // Called when a mutator starts and finishes, respectively.
    void Init()
    {
        observerCnt = 0;
        mutatorPhase.store(GCPhase::GC_PHASE_IDLE);
        inManagedContext.store(true);
    }

    ~Mutator()
    {
        tid = 0;
        stackBoundAddr = nullptr;
        if (satbNode != nullptr) {
            SatbBuffer::Instance().RetireNode(satbNode);
            satbNode = nullptr;
        }
    }

    static Mutator* NewMutator()
    {
        Mutator* mutator = new (std::nothrow) Mutator();
        CHECK_DETAIL(mutator != nullptr, "new Mutator failed");
        mutator->Init();
        mutator->SetMutatorPhase(Heap::GetHeap().GetGCPhase());
        return mutator;
    }

    void ResetMutator()
    {
        rawObject.object = nullptr;
        if (satbNode != nullptr) {
            SatbBuffer::Instance().RetireNode(satbNode);
            satbNode = nullptr;
        }
        uwContext.Reset();
        exceptionWrapper.ClearInfo();
    }

    static Mutator* GetMutator() noexcept;
    void StackGuardExpand() const;
    void StackGuardRecover() const;

    bool IsStackAddr(uintptr_t addr);
    void RecordStackPtrs(std::set<BaseObject**>& resSet);
    intptr_t FixExtendedStack(intptr_t frameBase = 0, uint32_t adjustedSize = 0, void* ip = nullptr);

    void InitTid()
    {
        tid = ThreadLocal::GetThreadLocalData()->tid;
        if (tid == 0) {
            tid = static_cast<uint32_t>(MapleRuntime::GetTid());
            ThreadLocal::GetThreadLocalData()->tid = tid;
        }
    }
    void SetCjthreadPtr(void* cjthreadPtr) { this->cjthread = cjthreadPtr;}
    uint32_t GetTid() const { return tid; }
    void* GetCjthreadPtr() const {return cjthread;}
    void InitProtectStackAddr();

    // Sets saferegion state of this mutator.
    __attribute__((always_inline)) inline void SetInSaferegion(SaferegionState state)
    {
        // assure sequential execution of setting insaferegion state and checking suspended state.
        inSaferegion.store(state, std::memory_order_seq_cst);
    }

    // Returns true if this mutator is in saferegion, otherwise false.
    __attribute__((always_inline)) inline bool InSaferegion() const
    {
        return inSaferegion.load(std::memory_order_seq_cst) != SAFE_REGION_FALSE;
    }

    inline void IncObserver() { observerCnt.fetch_add(1); }

    inline void DecObserver() { observerCnt.fetch_sub(1); }

    // Return true indicate there are some observer is visitting this mutator
    inline bool HasObserver() { return observerCnt.load() != 0; }

    inline size_t GetObserverCount() const { return observerCnt.load(); }

    // This interface can only be invoked in current thread environment.
#ifdef _WIN64
    __attribute__((always_inline)) void UpdateUnwindContext()
    {
        Runtime& runtime = Runtime::Current();
        WinModuleManager& winModuleManager = runtime.GetWinModuleManager();
        Uptr rip = 0;
        Uptr rsp = 0;
        GetContextWin64(&rip, &rsp);
        FrameInfo curFrame = GetCurFrameInfo(winModuleManager, rip, rsp);
        UnwindContextStatus ucs = uwContext.GetUnwindContextStatus();
        uwContext.frameInfo = GetCallerFrameInfo(winModuleManager, curFrame.mFrame, ucs);
    }
#else
    __attribute__((always_inline)) void UpdateUnwindContext()
    {
        void* ip = __builtin_return_address(0);
        void* fa = __builtin_frame_address(0);
        uwContext.frameInfo.mFrame.SetIP(static_cast<uint32_t*>(ip));
        uwContext.frameInfo.mFrame.SetFA(static_cast<FrameAddress*>(fa)->callerFrameAddress);
    }
#endif

    // Force current mutator enter saferegion, internal use only.
    __attribute__((always_inline)) inline void DoEnterSaferegion();
    // Force current mutator leave saferegion, internal use only.
    __attribute__((always_inline)) inline void DoLeaveSaferegion()
    {
        SetInSaferegion(SAFE_REGION_FALSE);
        // go slow path if the mutator should suspend
        if (UNLIKELY(HasAnySuspensionRequest())) {
            HandleSuspensionRequest();
        }
    }

    // If current mutator is not in saferegion, enter and return true
    // If current mutator has been in saferegion, return false
    __attribute__((always_inline)) inline bool EnterSaferegion(bool updateUnwindContext) noexcept;
    // If current mutator is in saferegion, leave and return true
    // If current mutator has left saferegion, return false
    __attribute__((always_inline)) inline bool LeaveSaferegion() noexcept;

    // Called if current mutator should do corresponding task by suspensionFlag value
    void HandleSuspensionRequest();
    // Called if current mutator should handle stw request
    void SuspendForSync();
    void SuspendForPreempt()
    {
        uwContext.SetUnwindContextStatus(UnwindContextStatus::RELIABLE);
        CJThreadPreemptResched();
    }

    bool IsVaildCJThread()
    {
        if (cjthread == nullptr) {
            return false;
        }
        return true;
    }
    unsigned long long int GetCJThreadId()
    {
        unsigned long long int id = CJThreadGetId(cjthread);
        return id;
    }

    char* GetCJThreadName()
    {
        char* name = CJThreadGetName(cjthread);
        if (name == nullptr || name[0] == '\0') {
            return nullptr;
        }
        return name;
    }

    __attribute__((always_inline)) inline bool FinishedTransition() const
    {
        return transitionState == FINISH_TRANSITION;
    }

    __attribute__((always_inline)) inline bool FinishedCpuProfile() const
    {
        return cpuProfileState.load(std::memory_order_acquire) == FINISH_CPUPROFILE;
    }

    __attribute__((always_inline)) inline void SetCpuProfileState(CpuProfileState state)
    {
        cpuProfileState.store(state, std::memory_order_relaxed);
    }

    __attribute__((always_inline)) inline void SetSuspensionFlag(SuspensionType flag)
    {
        if (flag == SUSPENSION_FOR_GC_PHASE) {
            transitionState.store(NEED_TRANSITION, std::memory_order_relaxed);
        } else if (flag == SUSPENSION_FOR_CPU_PROFILE) {
            cpuProfileState.store(NEED_CPUPROFILE, std::memory_order_relaxed);
        }
        suspensionFlag.fetch_or(flag, std::memory_order_seq_cst);
    }

    __attribute__((always_inline)) inline void ClearSuspensionFlag(SuspensionType flag)
    {
        suspensionFlag.fetch_and(~flag, std::memory_order_seq_cst);
    }

    __attribute__((always_inline)) inline uint32_t GetSuspensionFlag() const
    {
        return suspensionFlag.load(std::memory_order_acquire);
    }

    __attribute__((always_inline)) inline bool HasSuspensionRequest(SuspensionType flag) const
    {
        return (suspensionFlag.load(std::memory_order_acquire) & flag) != 0;
    }

    // should be merged to SuspensionType.
    __attribute__((always_inline)) inline bool HasPreemptRequest() const
    {
        if (uwContext.GetUnwindContextStatus() == UnwindContextStatus::SIGNAL_STATUS) {
            return reinterpret_cast<uintptr_t>(ThreadLocal::GetPreemptFlag()) == PREEMPT_DO_FLAG;
        }
        return false;
    }

    // Check whether current mutator needs to be suspended for GC or other request
    __attribute__((always_inline)) inline bool HasAnySuspensionRequest() const
    {
        return (suspensionFlag.load(std::memory_order_acquire) != 0) || HasPreemptRequest();
    }

    void SetSafepointStatePtr(uint64_t* slot) { safepointStatePtr = slot; }

    void SetSafepointActive(bool value)
    {
        uint64_t* statePtr = safepointStatePtr;
        if (statePtr == nullptr) {
            return;
        }
        *statePtr = static_cast<uint64_t>(value);
    }

    // Spin wait phase transition finished when GC is tranverting this mutator's phase
    __attribute__((always_inline)) inline void WaitForPhaseTransition() const
    {
        GCPhaseTransitionState state = transitionState.load(std::memory_order_acquire);
        while (state != FINISH_TRANSITION) {
            if (state != IN_TRANSITION) {
                LOG(RTLOG_INFO, "transition state has been reset for a second transition");
                return;
            }
            // Give up CPU to avoid overloading
            (void)sched_yield();
            state = transitionState.load(std::memory_order_acquire);
        }
    }

    __attribute__((always_inline)) inline void WaitForCpuProfiling() const
    {
        while (cpuProfileState.load(std::memory_order_acquire) != FINISH_CPUPROFILE) {
            // Give up CPU to avoid overloading
            (void)sched_yield();
        }
    }

    inline void GcPhaseEnum(GCPhase newPhase);
    inline void GCPhasePreForward(GCPhase newPhase);
    inline void HandleGCPhase(GCPhase newPhase);

    inline void HandleCpuProfile();

    void TransitionToGCPhaseExclusive(GCPhase newPhase);

    void TransitionToCpuProfileExclusive();

    // Ensure that mutator phase is changed only once by mutator itself or GC
    __attribute__((always_inline)) inline bool TransitionGCPhase(bool bySelf);

    __attribute__((always_inline)) inline bool TransitionToCpuProfile(bool bySelf);

    __attribute__((always_inline)) inline void SetMutatorPhase(const GCPhase newPhase)
    {
        mutatorPhase.store(newPhase, std::memory_order_release);
    }

    __attribute__((always_inline)) inline GCPhase GetMutatorPhase() const
    {
        return mutatorPhase.load(std::memory_order_acquire);
    }

    void VisitMutatorRoots(const RootVisitor& visitor)
    {
        VisitStackRoots(visitor);
        VisitExceptionRoots(visitor);
    }

    void VisitHeapReferences(const RootVisitor& rootVisitor, const DerivedPtrVisitor& derivedPtrVisitor);

    void DumpMutator() const
    {
        LOG(RTLOG_ERROR, "mutator %p: inSaferegion %x, tid %u, observerCnt %zu, gc phase: %u, suspension request %u",
            this, inSaferegion.load(std::memory_order_relaxed), tid, observerCnt.load(), mutatorPhase.load(),
            suspensionFlag.load());
    }

    // Init after fork.
    void InitAfterFork()
    {
        // tid changed after fork, so we re-initialize it.
        InitTid();
    }

    const void* GetSafepointPage() const
    {
        return safepointStatePtr;
    }

    UnwindContext& GetUnwindContext() { return uwContext; }

    ExceptionWrapper& GetExceptionWrapper() { return exceptionWrapper; }

#if defined(GCINFO_DEBUG) && GCINFO_DEBUG
    void PushFrameInfoForTrace(const GCInfoNode& frameGCInfo) { gcInfos.PushFrameInfoForTrace(frameGCInfo); }

    void PushFrameInfoForTrace(const GCInfoNode&& frameGCInfo) { gcInfos.PushFrameInfoForTrace(frameGCInfo); }

    void PushFrameInfoForFix(const GCInfoNodeForFix& frameGCInfo) { gcInfos.PushFrameInfoForFix(frameGCInfo); }

    void PushFrameInfoForFix(const GCInfoNodeForFix&& frameGCInfo) { gcInfos.PushFrameInfoForFix(frameGCInfo); }

    void DumpGCInfos() const
    {
        DLOG(ENUM, "dump mutator gc info thread id: %d", tid);
        gcInfos.DumpGCInfos();
    }
#endif

    bool IsManagedContext() const { return inManagedContext.load(std::memory_order_acquire); }

    void SetManagedContext(bool isManagedContext);

    ATTR_NO_INLINE void RememberObjectInSatbBuffer(const BaseObject* obj) { RememberObjectImpl(obj); }

    const SatbBuffer::Node* GetSatbBufferNode() const { return satbNode; }

    void ClearSatbBufferNode()
    {
        if (satbNode == nullptr) {
            return;
        }
        satbNode->Clear();
    }

    inline uintptr_t GetStackTopAddr() { return stackTopAddr; }
    inline void SetStackTopAddr(uintptr_t sta) { stackTopAddr = sta; }
    inline uintptr_t GetStackSize() { return stackSize; }
    inline void SetStackSize(uintptr_t ss) { stackSize = ss; }
    inline uintptr_t GetStackBaseAddr() { return stackBaseAddr; }
    inline void SetStackBaseAddr(uintptr_t sba) { stackBaseAddr = sba; }
    void InitStackInfo(ThreadLocalData* threadData);
#ifdef _WIN64
    uint32_t GetStackGrowFrameSize() { return stackGrowFrameSize; }
    void SetStackGrowFrameSize(uint32_t sgfs) { stackGrowFrameSize = sgfs; }
#endif

    void PushRawObject(BaseObject* obj) { rawObject.object = obj; }

    BaseObject* PopRawObject()
    {
        BaseObject* obj = rawObject.object;
        rawObject.object = nullptr;
        return obj;
    }
    void MutatorLock() { mutatorLock.lock(); }

    void MutatorUnlock() { mutatorLock.unlock(); }

    void PreparedToRun(ThreadLocalData* tlData)
    {
        if (UNLIKELY(tlData->buffer == nullptr)) {
            (void)AllocBuffer::GetOrCreateAllocBuffer();
        }
        DoLeaveSaferegion();
        SetSafepointStatePtr(&tlData->safepointState);
        SetSafepointActive(false);
    }

    void PreparedToPark(void* pc, void* fa)
    {
        SetSafepointStatePtr(nullptr);
        if (UNLIKELY((uwContext.GetUnwindContextStatus() == UnwindContextStatus::RISKY) || InSaferegion())) {
            return;
        }
#if defined(__linux__) || defined(hongmeng) || defined(__APPLE__)
        if (LIKELY(uwContext.GetUnwindContextStatus() != UnwindContextStatus::RISKY)) {
            uwContext.frameInfo.mFrame.SetIP(reinterpret_cast<const uint32_t*>(pc));
            uwContext.frameInfo.mFrame.SetFA(reinterpret_cast<FrameAddress*>(fa));
        }
#elif defined(_WIN64)
        if (LIKELY(uwContext.GetUnwindContextStatus() != UnwindContextStatus::RISKY)) {
            Uptr rip = reinterpret_cast<Uptr>(pc);
            Uptr rsp = reinterpret_cast<Uptr>(fa);
            Runtime& runtime = Runtime::Current();
            WinModuleManager& winModuleManager = runtime.GetWinModuleManager();
            FrameInfo curFrame = GetCurFrameInfo(winModuleManager, rip, rsp);
            UnwindContextStatus ucs = uwContext.GetUnwindContextStatus();
            uwContext.frameInfo = GetCallerFrameInfo(winModuleManager, curFrame.mFrame, ucs);
        }
#endif // platform
        SetInSaferegion(SaferegionState::SAFE_REGION_TRUE);
    }

protected:
    // for managed stack
    void VisitStackRoots(const RootVisitor& func);
    void VisitHeapReferencesOnStack(const RootVisitor& rootVisitor, const DerivedPtrVisitor& derivedPtrVisitor);
    // for exception ref
    void VisitExceptionRoots(const RootVisitor& func);
    void VisitRawObjects(const RootVisitor& func);
    void CreateCurrentGCInfo();

private:
    void RememberObjectImpl(const BaseObject* obj)
    {
        if (LIKELY(Heap::IsHeapAddress(obj))) {
            if (SatbBuffer::Instance().ShouldEnqueue(obj)) {
                SatbBuffer::Instance().EnsureGoodNode(satbNode);
                satbNode->Push(obj);
            }
        }
    }
    // Indicate the current mutator phase and use which barrier in concurrent gc
    // ATTENTION: THE LAYOUT FOR GCPHASE MUST NOT BE CHANGED!
    std::atomic<GCPhase> mutatorPhase = { GCPhase::GC_PHASE_UNDEF };
    // thread id
    uint32_t tid = 0;
    // cjthread ptr
    void* cjthread;
    // in saferegion, it will not access any managed objects and can be visitted by observer
    std::atomic<uint32_t> inSaferegion = { SAFE_REGION_TRUE };
    // Protect observerCnt
    std::mutex observeCntMutex;
    // Increase when this mutator is observed by some observer
    std::atomic<size_t> observerCnt = { 0 };
    // context for unwinding stack
    UnwindContext uwContext;
    // exception object wrapper
    ExceptionWrapper exceptionWrapper;
#ifndef __WIN64
    void* unuse = nullptr; // reusable placeholder
#endif // __WIN64

    uint64_t* safepointStatePtr = nullptr; // state: active or not
#ifdef _WIN64
    uint32_t stackGrowFrameSize = 0;
#endif

    // If set implies this mutator should process suspension requests
    std::atomic<uint32_t> suspensionFlag = { 0 };
    // Indicate the state of mutator's phase transition
    std::atomic<GCPhaseTransitionState> transitionState = { NO_TRANSITION };
    ObjectRef rawObject{ nullptr };

    SatbBuffer::Node* satbNode = nullptr;
#if defined(GCINFO_DEBUG) && GCINFO_DEBUG
    GCInfos gcInfos;
#endif
    // this flag is used for gc unwind stack, when runtime-thread stack doesn't include managed frame,
    // we don't need to scan it.
    std::atomic<bool> inManagedContext = { true };
    void* stackBoundAddr = { nullptr };
    std::mutex mutatorLock;

    uintptr_t stackBaseAddr = 0;
    uintptr_t stackTopAddr = 0;
    uintptr_t stackSize = 0;

    std::atomic<CpuProfileState> cpuProfileState = { NO_CPUPROFILE };
};

// This function is mainly used to initialize the context of mutator.
// Ensured that updated fa is the caller layer of the managed function to be called.
extern "C" void MRT_PreRunManagedCode(Mutator* mutator, int layers,
                                      ThreadLocalData* threadData);
} // namespace MapleRuntime

#endif // MRT_MUTATOR_H
