// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#include <condition_variable>
#include <thread>
#include <set>
#if defined(_WIN64)
#include <windows.h>
#elif defined(__APPLE__)
#include "sys/sysctl.h"
#else
#include "sys/sysinfo.h"
#endif

#include "Base/Log.h"
#include "Cangjie.h"
#include "Concurrency/Concurrency.h"
#include "ExceptionManager.inline.h"
#include "Mutator/Mutator.h"
#include "Mutator/MutatorManager.h"
#include "Heap/Collector/CollectorResources.h"
#include "RuntimeConfig.h"
#include "UnwindStack/MangleNameHelper.h"
#if defined(__OHOS__) && (__OHOS__ == 1)
#include "Inspector/FileStream.h"
#include "Inspector/ProfilerAgentImpl.h"
#endif
#ifndef _WIN64
#include "SignalManager.h"
#endif
#include "schedule.h"
#if defined(CANGJIE_SANITIZER_SUPPORT)
#include "Sanitizer/SanitizerInterface.h"
#endif
#include "CpuProfiler/CpuProfiler.h"
#include "Heap/Collector/GcRequest.h"
#include "Common/ScopedObjectAccess.h"
#include "HeapManager.h"
#include "HeapManager.inline.h"

CANGJIE_RT_API_DECLS_BEGIN
const double ERRORESTIMATE = 1e-12;

static std::atomic_bool g_runtimeInited = ATOMIC_VAR_INIT(false);
static std::atomic_bool g_runtimeFinished = ATOMIC_VAR_INIT(false);
static std::condition_variable g_conditionVariable;
static std::mutex g_mtx;
static size_t g_sysmemSize = 1U * MapleRuntime::GB;
static std::set<uintptr_t> futureSet;

static void CheckSysmemSize()
{
#if defined(_WIN64)
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    bool ret = GlobalMemoryStatusEx(&memInfo);
    if (ret) {
        g_sysmemSize = memInfo.ullTotalPhys;
    } else {
        LOG(RTLOG_ERROR, "Get system memory failed.\n");
    }
#elif defined(__APPLE__)
    const int sz = 2;
    int mib[sz];
    size_t length = sizeof(g_sysmemSize);
    mib[0] = CTL_HW;
    mib[1] = HW_MEMSIZE;
    sysctl(mib, sz, &g_sysmemSize, &length, nullptr, 0);
#else
    struct sysinfo memInfo;
    int ret = sysinfo(&memInfo);
    if (ret == 0) {
        g_sysmemSize = memInfo.totalram * memInfo.mem_unit;
    } else {
        LOG(RTLOG_ERROR, "Get system memory failed. msg: %s.\n", strerror(errno));
    }
#endif
}

static bool CheckInitConfig(const struct RuntimeParam& param)
{
    const size_t maxStackSize = 1U * 1024 * 1024; // 1GB
    // Global init func need stack size no less than 64KB.
    if ((param.coParam.coStackSize < 64 || param.coParam.coStackSize > maxStackSize) &&
        param.coParam.coStackSize != 0) {
        LOG(RTLOG_ERROR, "RuntimeParam.coParam.coStackSize must be in range [64KB, 1GB].\n");
        return false;
    }
    size_t paramHeapSize = param.heapParam.heapSize * MapleRuntime::KB;
    // Check heap configuration, min heapsize 4MB.
    if (paramHeapSize != 0 && (paramHeapSize < 4 * MapleRuntime::MB || paramHeapSize > g_sysmemSize)) {
        LOG(RTLOG_ERROR, "RuntimeParam.heapParam.heapSize must be in range [4MB, system memory size].\n");
        return false;
    }
    // Region size must be in range [system page size, 2048KB].
    const size_t minRegionSize = MapleRuntime::MRT_PAGE_SIZE / MapleRuntime::KB;
    const size_t maxRegionSize = 2048; // 2048KB
    if (param.heapParam.regionSize != 0 &&
        (param.heapParam.regionSize < minRegionSize || param.heapParam.regionSize > maxRegionSize)) {
        LOG(RTLOG_ERROR, "RuntimeParam.heapParam.regionSize must be in range [%zuKB, 2048KB].\n", minRegionSize);
        return false;
    }
    // Live region threshold must be in range (0, 1].
    if (param.heapParam.exemptionThreshold - 0.0 < -ERRORESTIMATE &&
    (param.heapParam.exemptionThreshold <= -ERRORESTIMATE || param.heapParam.exemptionThreshold - 1 > ERRORESTIMATE)) {
        LOG(RTLOG_ERROR, "RuntimeParam.heapParam.exemptionThreshold must be in range (0, 1].\n");
        return false;
    }
    // Heap utilization must be in range (0, 1].
    if (param.heapParam.heapUtilization - 0.0 < -ERRORESTIMATE &&
        (param.heapParam.heapUtilization <= -ERRORESTIMATE || param.heapParam.heapUtilization - 1 > ERRORESTIMATE)) {
        LOG(RTLOG_ERROR, "RuntimeParam.heapParam.heapUtilization must be in range (0, 1].\n");
        return false;
    }
    // Garbage ration must be in range [0.1, 1].
    const double minGarbageRation = 0.1;
    const double maxGarbageRation = 1;
    if ((param.gcParam.garbageThreshold - 0.0 < -ERRORESTIMATE ||
        param.gcParam.garbageThreshold - 0.0 > ERRORESTIMATE) &&
        (param.gcParam.garbageThreshold - minGarbageRation < ERRORESTIMATE
            || param.gcParam.garbageThreshold - maxGarbageRation > ERRORESTIMATE)) {
        LOG(RTLOG_ERROR, "RuntimeParam.gcParam.garbageThreshold must be in range [0.1, 1].\n");
        return false;
    }
    return true;
}

RTErrorCode SetRuntimeInitFlag()
{
    if (g_runtimeFinished.load()) {
        LOG(RTLOG_ERROR, "Cangjie runtime has been finished and don't support init again.");
        return E_FAILED;
    }
    if (g_runtimeInited.load()) {
        LOG(RTLOG_ERROR, "Cangjie runtime has been initialized and don't support init again.");
        return E_FAILED;
    }
    g_runtimeInited.store(true);
    return E_OK;
}

RTErrorCode SetRuntimeFiniFlag()
{
    if (!g_runtimeInited.load()) {
        LOG(RTLOG_ERROR, "Cangjie runtime has not been initialized when executing finish.");
        return E_FAILED;
    }
    if (g_runtimeFinished.load()) {
        LOG(RTLOG_ERROR, "Cangjie runtime has been finished and don't support finish again.");
        return E_FAILED;
    }
    g_runtimeFinished.store(true);
    return E_OK;
}

static void* StartCJRuntime(void* arg)
{
    RuntimeParam* p = static_cast<RuntimeParam*>(arg);
    MapleRuntime::CangjieRuntime::CreateAndInit(*p);
    {
        // Runtime initializes completely, notify the main thread to continue to run.
        std::unique_lock<std::mutex> lck(g_mtx);
        g_runtimeInited.store(true);
        g_conditionVariable.notify_all();
    }
    MapleRuntime::ThreadLocal::SetCJProcessorFlag(true);
    ScheduleStart();
    return nullptr;
}

RTErrorCode InitCJRuntime(const struct RuntimeParam* param)
{
    if (g_runtimeFinished.load()) {
        LOG(RTLOG_ERROR, "Cangjie runtime has been finished and don't support init again.");
        return E_FAILED;
    }
    // Runtime has been initialized, return 0 directly.
    if (g_runtimeInited.load()) {
        LOG(RTLOG_ERROR, "Cangjie runtime has been initialized and don't support init again.");
        return E_OK;
    }
#if defined(GENERAL_ASAN_SUPPORT_INTERFACE)
    MapleRuntime::Sanitizer::AsanRead(param, sizeof(struct RuntimeParam));
#endif
    CheckSysmemSize();
    if (!CheckInitConfig(*param)) {
        return E_ARGS;
    }
#ifdef _WIN64
    size_t defaultStackSize = 128; // default 128KB in windows, measured in KB
#elif defined(__OHOS__) || defined(__HOS__)
    size_t defaultStackSize = 1024; // default 1MB in OHOS, measured in KB
#else
    size_t defaultStackSize = 128; // default 128KB, measured in KB
#endif
    unsigned int cpus = std::thread::hardware_concurrency();
    uint32_t defaultProcs = cpus != 0 ? static_cast<uint32_t>(cpus) : 8;
    size_t initHeapSize = param->heapParam.heapSize == 0 ? 64 * 1024 : param->heapParam.heapSize;
    RuntimeParam config = {
        .heapParam = {
            // Default value of region size is 64KB.
            .regionSize = param->heapParam.regionSize == 0 ? 64 : param->heapParam.regionSize,
            // Default value of heap size is 64 * 1024KB.
            .heapSize = initHeapSize,
            // Default value of live region threshold is 80%.
            .exemptionThreshold = param->heapParam.exemptionThreshold < ERRORESTIMATE ? \
                0.8 : param->heapParam.exemptionThreshold,
            // Default value of heap utilization is 80%.
            .heapUtilization = param->heapParam.heapUtilization < ERRORESTIMATE ? \
                0.8 : param->heapParam.heapUtilization,
            // Default value of heap growth is 1 + 0.15.
            .heapGrowth = param->heapParam.heapGrowth < ERRORESTIMATE ? 0.15 : param->heapParam.heapGrowth,
            // Default value of allocation rate is 1024MB/s.
            .allocationRate = param->heapParam.allocationRate < ERRORESTIMATE ? 10240 : param->heapParam.allocationRate,
            // Default value of allocation wait time is 1000ns.
            .allocationWaitTime = param->heapParam.allocationWaitTime == 0 ? 1000 : param->heapParam.allocationWaitTime,
        },
        .gcParam = {
            // Default value of gc threshold is heapSize.
            .gcThreshold = param->gcParam.gcThreshold == 0 ? \
                initHeapSize * MapleRuntime::KB : param->gcParam.gcThreshold * MapleRuntime::KB,
            // Default value of garbage ratio is 50%.
            .garbageThreshold = param->gcParam.garbageThreshold < ERRORESTIMATE ? 0.5 : param->gcParam.garbageThreshold,
            // Default value of GC interval is 150ms.
            .gcInterval = param->gcParam.gcInterval == 0 ? 150 * MapleRuntime::MILLI_SECOND_TO_NANO_SECOND :
                param->gcParam.gcInterval * MapleRuntime::MILLI_SECOND_TO_NANO_SECOND,
            // Default value of backup GC interval is 240s.
            .backupGCInterval = param->gcParam.backupGCInterval == 0 ? 240 * MapleRuntime::SECOND_TO_NANO_SECOND :
                param->gcParam.backupGCInterval * MapleRuntime::SECOND_TO_NANO_SECOND,
            // Default GC threads factor is 2.
            .gcThreads = param->gcParam.gcThreads == 0 ? 2 : param->gcParam.gcThreads,
        },
        .logParam = {
            .logLevel = param->logParam.logLevel,
        },
        .coParam = {
            // Default value of thread stack size is 1 * 1024KB
            .thStackSize = param->coParam.thStackSize == 0 ? 1 * 1024 : param->coParam.thStackSize,
            .coStackSize = param->coParam.coStackSize == 0 ? defaultStackSize : param->coParam.coStackSize,
            .processorNum = param->coParam.processorNum == 0 ? defaultProcs : param->coParam.processorNum,
        }
    };

    pthread_t thread;
    pthread_attr_t attr;
    size_t stackSize = config.coParam.thStackSize * MapleRuntime::KB;
    ScheduleHandle scheduler = NULL;
#if defined(__linux__) || defined(hongmeng) || defined(__APPLE__)
    // PTHREAD_STACK_MIN is not supported in Windows.
    if (stackSize < PTHREAD_STACK_MIN) {
        stackSize = PTHREAD_STACK_MIN;
    }
#endif
    CHECK_PTHREAD_CALL(pthread_attr_init, (&attr), "init pthread attr");
    CHECK_PTHREAD_CALL(pthread_attr_setdetachstate, (&attr, PTHREAD_CREATE_JOINABLE), "set pthread joinable");
    CHECK_PTHREAD_CALL(pthread_attr_setstacksize, (&attr, stackSize), "set pthread stacksize");
    CHECK_PTHREAD_CALL(pthread_create, (&thread, &attr, StartCJRuntime, &config), "create StartCJRuntime thread");
    CHECK_PTHREAD_CALL(pthread_attr_destroy, (&attr), "destroy pthread attr");

    // Waiting for runtime initialize completely before continue.
    std::unique_lock<std::mutex> lck(g_mtx);
    g_conditionVariable.wait(lck, [] { return g_runtimeInited.load(); });
    while (!ScheduleIsRunning(scheduler)) {
        scheduler = MapleRuntime::Runtime::Current().GetConcurrencyModel().GetThreadScheduler();
    }
    ScheduleSetToCurrentThread(scheduler);
    return E_OK;
}

void* InitUIScheduler()
{
    if (!g_runtimeInited.load()) {
        LOG(RTLOG_ERROR, "Cangjie runtime should be initialized when initialize a sub scheduler.");
        return nullptr;
    }
    auto runtime = reinterpret_cast<MapleRuntime::CangjieRuntime*>(&MapleRuntime::Runtime::Current());
    auto scheduler = runtime->CreateSingleThreadScheduler();
    if (scheduler == nullptr) {
        LOG(RTLOG_ERROR, "Fail to create and init a sub scheduler.");
        return nullptr;
    }
    return scheduler;
}

RTErrorCode RunUIScheduler(unsigned long long timeout)
{
    if (!g_runtimeInited.load()) {
        LOG(RTLOG_ERROR, "Cangjie runtime should be initialized when run a sub scheduler.");
        return E_FAILED;
    }
    auto result = ScheduleStartNoWait(timeout);
    return result == 0 ? E_OK : E_FAILED;
}

RTErrorCode SetCJCommandLineArgs(int argc, const char* argv[])
{
    if (!g_runtimeInited.load()) {
        LOG(RTLOG_ERROR, "Cangjie runtime should be initialized when setting command line args.");
        return E_FAILED;
    }
    if (argc <= 0 || argv == nullptr) {
        LOG(RTLOG_WARNING, "Cangjie command line args are not set.");
        return E_OK;
    }
#if defined(GENERAL_ASAN_SUPPORT_INTERFACE)
    for (int i = 0; i < argc; ++i) {
        // just verify the address is right
        MapleRuntime::Sanitizer::AsanRead(argv[i], 1);
    }
#endif
    reinterpret_cast<MapleRuntime::CangjieRuntime*>(&MapleRuntime::Runtime::Current())->SetCommandLinesArgs(argc, argv);
    return E_OK;
}

// The function will stop scheduler and finish runtime.
RTErrorCode FiniCJRuntime()
{
    if (!g_runtimeInited.load()) {
        LOG(RTLOG_ERROR, "Cangjie runtime has not been initialized when executing finish.");
        return E_FAILED;
    }
    if (g_runtimeFinished.load()) {
        LOG(RTLOG_ERROR, "Cangjie runtime has been finished and don't support finish again.");
        return E_OK;
    }
    g_runtimeFinished.store(true);
    // Ensure that sampling thread is stopped before finishing runtime.
    MapleRuntime::CpuProfiler::GetInstance().TryStopSampling();

    MapleRuntime::Mutator* mutator = MapleRuntime::Mutator::GetMutator();
    if (mutator != nullptr) {
        MapleRuntime::MutatorManager::Instance().TransitMutatorToExit();
    }
    ScheduleHandle scheduler = MapleRuntime::Runtime::Current().GetConcurrencyModel().GetThreadScheduler();
    ScheduleStopOutside(scheduler);
    MapleRuntime::CangjieRuntime::FiniAndDelete();
    return E_OK;
}

enum FutureFlag : uint8_t {
    FLAG_WAITING,
    FLAG_DONE,
    FLAG_RELEASED,
};

struct FutureImpl {
    const CJTaskFunc fn;
    void* arg;
    void* res{ nullptr };
    FutureFlag flag{ FLAG_WAITING };
    std::mutex g_mtx;
    std::condition_variable cv;

    explicit FutureImpl(void* arg, const CJTaskFunc fn = nullptr) : fn(fn), arg(arg) {}
};

static void* UserFuncExecutor(void* arg, [[maybe_unused]] unsigned int len)
{
    auto lwtData = static_cast<MapleRuntime::LWTData*>(arg);
    auto fi = static_cast<FutureImpl*>(lwtData->obj);
    uintptr_t threadData = MapleRuntime::MRT_GetThreadLocalData();
    // mutator has been set to a valid pointer before.
    MapleRuntime::Mutator* mutator = reinterpret_cast<MapleRuntime::ThreadLocalData*>(threadData)->mutator;
    MapleRuntime::MRT_PreRunManagedCode(mutator, 0, reinterpret_cast<MapleRuntime::ThreadLocalData*>(threadData));
    void* ptr = MapleRuntime::ExecuteCangjieStub(fi->arg, 0, 0, reinterpret_cast<void*>(fi->fn),
                                                 reinterpret_cast<void*>(threadData), 0);
#ifdef CANGJIE_TSAN_SUPPORT
    MapleRuntime::Sanitizer::TsanFinalize();
#endif
    MapleRuntime::ExceptionManager::CheckAndDumpException();
    MapleRuntime::Runtime::Current().GetMutatorManager().TransitMutatorToExit();

    bool needDelete = false;
    // Set the return value and notify possible GetTaskRet to return, or delete the future directly.
    {
        std::lock_guard<std::mutex> lck(fi->g_mtx);
        fi->res = ptr;
        if (fi->flag == FLAG_RELEASED) {
            needDelete = true;
        } else {
            fi->flag = FLAG_DONE;
        }
        fi->cv.notify_all();
    }
    if (needDelete) {
        std::lock_guard<std::mutex> lock(g_mtx);
        auto it = futureSet.find(reinterpret_cast<uintptr_t>(fi));
        futureSet.erase(it);
        delete fi;
    }
    return ptr;
}

bool CheckRuntimeValid(const CJTaskFunc func)
{
    if (func == nullptr) {
        LOG(RTLOG_ERROR, "task func can not be nullptr.\n");
        return false;
    }
    if (!g_runtimeInited.load()) {
        LOG(RTLOG_ERROR, "Cangjie runtime has not been initialized.\n");
        return false;
    }
    if (g_runtimeFinished.load()) {
        LOG(RTLOG_ERROR, "Cangjie runtime has been finished.\n");
        return false;
    }
    return true;
}

extern "C" MRT_EXPORT bool MRT_CheckRuntimeFinished()
{
    return g_runtimeFinished.load();
}

ScheduleHandle GetScheduler()
{
    ScheduleHandle scheduler = MapleRuntime::Runtime::Current().GetConcurrencyModel().GetThreadScheduler();
    if (scheduler == nullptr) {
        LOG(RTLOG_ERROR, "failed to get scheduler.\n");
        return nullptr;
    }
    return scheduler;
}

CJThreadHandle RunCJTaskImpl(const CJTaskFunc func, void* args, int num = 0, CJThreadSpecificDataInner* data = nullptr,
                             ScheduleHandle schedule = nullptr, bool isSignal = false)
{
    MapleRuntime::ScopedEntryHiTrace hiTrace("CJRT_INVOKE_CJTASK_ASYNC");
    if (!CheckRuntimeValid(func)) {
        return nullptr;
    }
    ScheduleHandle scheduler = schedule == nullptr ? GetScheduler() : schedule;
    if (scheduler == nullptr) {
        return nullptr;
    }
    auto fi = new FutureImpl(args, func);
    if (UNLIKELY(fi == nullptr)) {
        LOG(RTLOG_ERROR, "new future failed.\n");
        return nullptr;
    }
    {
        std::lock_guard<std::mutex> lck(g_mtx);
        futureSet.insert(reinterpret_cast<uintptr_t>(fi));
    }
    CJThreadAttr attr;
    CJThreadAttrInit(&attr);
    CJThreadAttrNameSet(&attr, "cangjie");
    if (data != nullptr) {
#if defined(GENERAL_ASAN_SUPPORT_INTERFACE)
        MapleRuntime::Sanitizer::AsanRead(data, sizeof(CJThreadSpecificDataInner));
#endif
        if (CJThreadAttrSpecificSet(&attr, num, data) != 0) {
            LOG(RTLOG_ERROR, "failed to set cjthread specific, please check your input num.\n");
            return nullptr;
        }
    }

    MapleRuntime::LWTData lwtData;
    lwtData.execute = nullptr;
    lwtData.threadObject = nullptr;
    lwtData.obj = fi;
    CJThreadHandle handle = CJThreadNewToSchedule(scheduler, (const struct CJThreadAttr*)(&attr), UserFuncExecutor,
                                                  &lwtData, sizeof(lwtData), isSignal);
    if (handle == nullptr) {
        LOG(RTLOG_ERROR, "failed to create cjthread.\n");
        std::lock_guard<std::mutex> lck(g_mtx);
        auto it = futureSet.find(reinterpret_cast<uintptr_t>(fi));
        futureSet.erase(it);
        delete fi;
        return nullptr;
    }
    return fi;
}

// asan read is in RunCJTaskImpl, so no need to intercept here
CJThreadHandle RunCJTask(const CJTaskFunc func, void* args) { return RunCJTaskImpl(func, args); }
CJThreadHandle RunCJTaskSignal(const CJTaskFunc func, void* args)
{
    return RunCJTaskImpl(func, args, 0, nullptr, nullptr, true);
}

CJThreadHandle RunCJTaskToSchedule(const CJTaskFunc func, void* args, ScheduleHandle schedule)
{
    if (schedule == nullptr) {
        LOG(RTLOG_ERROR, "schedule is invalid, failed to create cjthread.\n");
        return nullptr;
    }
    return RunCJTaskImpl(func, args, 0, nullptr, schedule);
}

CJThreadHandle RunCJTaskWithLocal(const CJTaskFunc func, void* args, struct CJThreadSpecificData* data, int num)
{
    CJThreadSpecificDataInner* specificData = (CJThreadSpecificDataInner*)data;
    return RunCJTaskImpl(func, args, num, specificData);
}

static void* UserFuncWrapper(void* arg)
{
    auto fi = static_cast<FutureImpl*>(arg);
    uintptr_t threadData = MapleRuntime::MRT_GetThreadLocalData();
    MapleRuntime::Mutator* mutator = reinterpret_cast<MapleRuntime::ThreadLocalData*>(threadData)->mutator;
    MapleRuntime::MRT_PreRunManagedCode(mutator, 0, reinterpret_cast<MapleRuntime::ThreadLocalData*>(threadData));
    void* ptr = MapleRuntime::ExecuteCangjieStub(fi->arg, 0, 0, reinterpret_cast<void*>(fi->fn),
                                                 reinterpret_cast<void*>(threadData), 0);

    MapleRuntime::ExceptionManager::CheckAndDumpException();
    MapleRuntime::Runtime::Current().GetMutatorManager().TransitMutatorToExit();

    delete fi;
    return ptr;
}

SemiCJThreadHandle CreateCJThread(const CJTaskFunc func, void *arg, struct CJThreadSpecificData* data,
                                  int num)
{
    if (!CheckRuntimeValid(func)) {
        return nullptr;
    }
    ScheduleHandle scheduler = GetScheduler();
    if (scheduler == nullptr) {
        return nullptr;
    }
    auto fi = new FutureImpl(arg, func);
    if (UNLIKELY(fi == nullptr)) {
        LOG(RTLOG_ERROR, "new future failed.\n");
        return nullptr;
    }

    CJThreadAttr attr;
    CJThreadAttrInit(&attr);
    if (data != nullptr && CJThreadAttrSpecificSet(&attr, num, (CJThreadSpecificDataInner*)data) != 0) {
        LOG(RTLOG_ERROR, "failed to set CJThreadSpecificData, check in-arg num.\n");
        return nullptr;
    }

    CJThreadHandle handle = CJ_CJThreadCreate((const struct CJThreadAttr*)(&attr), UserFuncWrapper, fi);
    if (handle == nullptr) {
        LOG(RTLOG_ERROR, "failed to create cjthread.\n");
        return nullptr;
    }
    return handle;
}

int SuspendCJThread(void) { return CJ_CJThreadYield(); }

int ResumeCJThread(SemiCJThreadHandle co) { return CJ_CJThreadResume(co); }

int GetCJThreadState(SemiCJThreadHandle co) { return CJ_CJThreadStateGet(co); }

void *GetCJThreadResult(SemiCJThreadHandle co) { return CJ_CJThreadResultGet(co); }

int DestoryCJThread(SemiCJThreadHandle co) { return CJ_CJThreadDestroy(co); }

int SuspendSchedule(void)
{
    int ret = ScheduleSuspend();
    return ret == 0 ? E_OK : E_STATE;
}

int ResumeSchedule(void)
{
    int ret = ScheduleResume();
    return ret == 0 ? E_OK : E_STATE;
}

bool AnyTask(void) { return ScheduleAnyCJThreadOrTimer(); }

// NetPollNotifyAdd and NetPollNotifyDel was used for datacom, not available on Windows for the moment.
#if defined(__linux__) || defined(hongmeng)
void* NetPollNotifyAdd(int fd, int events, NetPollNotifyFunc func, void* arg)
{
    return SchdpollNotifyAdd(fd, events, func, arg);
}

int NetPollNotifyDel(int fd, void* pd) { return SchdpollNotifyDel(fd, pd); }
#endif

// asan read is in GetTaskRetWithTimeout, so no need to intercept here
int GetTaskRet(const CJThreadHandle handle, void** ret) { return GetTaskRetWithTimeout(handle, ret, int64_t(0)); }

int GetTaskRetWithTimeout(const CJThreadHandle handle, void** ret, int64_t timeout)
{
    if (handle == nullptr || ret == nullptr) {
        return E_ARGS;
    }
    auto fi = static_cast<FutureImpl*>(handle);
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        auto it = futureSet.find(reinterpret_cast<uintptr_t>(fi));
        if (it == futureSet.end()) {
            return E_ARGS;
        }
    }
#if defined(GENERAL_ASAN_SUPPORT_INTERFACE)
    MapleRuntime::Sanitizer::AsanRead(fi, sizeof(FutureImpl));
#endif
    if (fi->flag != FLAG_DONE && fi->flag != FLAG_WAITING) {
        return E_ARGS;
    }
    std::unique_lock<std::mutex> lck(fi->g_mtx);
#if defined(GENERAL_ASAN_SUPPORT_INTERFACE)
    MapleRuntime::Sanitizer::AsanWrite(ret, sizeof(fi->res));
#endif
    if (fi->flag == FLAG_DONE) {
        *ret = fi->res;
        return E_OK;
    }
    if (timeout <= 0) {
        fi->cv.wait(lck, [fi] { return fi->flag == FLAG_DONE; });
        *ret = fi->res;
        return E_OK;
    }
    bool got = fi->cv.wait_for(lck, std::chrono::nanoseconds(timeout), [fi] { return fi->flag == FLAG_DONE; });
    if (got) {
        *ret = fi->res;
        return E_OK;
    }
    return E_TIMEOUT;
}

void ReleaseHandle(const CJThreadHandle handle)
{
    if (handle == nullptr) {
        return;
    }
    auto fi = static_cast<FutureImpl*>(handle);
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        auto it = futureSet.find(reinterpret_cast<uintptr_t>(fi));
        if (it == futureSet.end()) {
            return;
        }
    }
#if defined(GENERAL_ASAN_SUPPORT_INTERFACE)
    MapleRuntime::Sanitizer::AsanRead(fi, sizeof(FutureImpl));
#endif
    if (fi->flag != FLAG_DONE && fi->flag != FLAG_WAITING) {
        return;
    }
    bool needDelete = false;
    {
        std::lock_guard<std::mutex> lck(fi->g_mtx);
        needDelete = (fi->flag != FLAG_WAITING);
        fi->flag = FLAG_RELEASED;
    }
    if (needDelete) {
        std::lock_guard<std::mutex> lock(g_mtx);
        futureSet.erase(reinterpret_cast<uintptr_t>(fi));
        delete fi;
    }
}

int LoadCJLibrary(const char* libName)
{
    if (libName == nullptr) {
        LOG(RTLOG_ERROR, "libName can NOT be NULL.\n");
        return E_ARGS;
    }
    void* retHandler = MapleRuntime::LoaderManager::GetInstance()->LoadCJLibrary(libName);
    if (retHandler == nullptr) {
        LOG(RTLOG_ERROR, "LoadCJLibrary fail.\n");
        return E_ARGS;
    }
#ifdef _WIN64
    if (g_runtimeInited.load()) {
        MapleRuntime::Runtime::Current().GetWinModuleManager().ReadWinModuleAtRunning();
    }
#endif
    return E_OK;
}

#if defined(__OHOS__) && (__OHOS__ == 1)
void RegisterUncaughtExceptionHandler(const CJUncaughtExceptionInfo& handler)
{
    MapleRuntime::ExceptionManager& exceptionManager = MapleRuntime::Runtime::Current().GetExceptionManager();
    exceptionManager.RegisterUncaughtExceptionHandler(handler);
}
#endif

char* CJ_MRT_DemangleHandle(const char* functionName)
{
    MapleRuntime::MangleNameHelper helper(functionName);
    helper.Demangle();
    MapleRuntime::CString methodname = helper.GetDemangleName();
    int32_t len = strlen(methodname.GetStr());
    // demangleName should be freed by caller.
    char* demangleName = reinterpret_cast<char*>(malloc(len + 1));
    if (demangleName == nullptr) {
        LOG(RTLOG_ERROR, "malloc demangleName failed, size is %d", len + 1);
        return nullptr;
    }
    int32_t ret = strcpy_s(demangleName, len + 1, methodname.GetStr());
    if (ret != EOK) {
        LOG(RTLOG_ERROR, "strcpy_s failed when copy func or file names");
        return nullptr;
    }
    return demangleName;
}

#if defined(__OHOS__) && (__OHOS__ == 1)
using SendMsgCB = std::function<void(const std::string& message)>;
void ProfilerAgent(const std::string& message, SendMsgCB sendMsg)
{
    MapleRuntime::ProfilerAgentImpl(message, sendMsg);
}
#endif

int LoadCJLibraryWithInit(const char* libName)
{
    if (LoadCJLibrary(libName) != E_OK) {
        LOG(RTLOG_ERROR, "LoadCJLibrary fail.\n");
        return E_ARGS;
    }
    int ret = InitCJLibrary(libName);
    if (ret != 0) {
        LOG(RTLOG_ERROR, "LoadCJLibrary fail. as InitCJLibrary return false\n");
        MapleRuntime::LoaderManager::GetInstance()->UnLoadLibrary(libName);
        return E_ARGS;
    }
    return E_OK;
}

int InitCJLibrary(const char* libName)
{
    if (!g_runtimeInited.load()) {
        LOG(RTLOG_ERROR, "runtime has NOT been initialized.\n");
        return E_ARGS;
    }

    if (libName == nullptr) {
        return E_ARGS;
    }
#if defined(__OHOS__)
    bool ret = MapleRuntime::MRT_CJLibInit(const_cast<char*>(libName));
    return ret ? E_OK : E_ARGS;
#else
    // run cj func should execute within cjthread environment,
    CJThreadHandle future =
        RunCJTask(reinterpret_cast<CJTaskFunc>(MapleRuntime::MRT_CJLibInit), const_cast<char*>(libName));
    if (future == nullptr) {
        LOG(RTLOG_ERROR, "InitCJLibrary fail. as RunCJTask return null\n");
        return E_ARGS;
    }
    void* ret = nullptr;
    GetTaskRet(future, reinterpret_cast<void**>(&ret));
    ReleaseHandle(future);
    if (ret != nullptr) {
        return E_OK;
    }
    return E_ARGS;
#endif
}

void* FindCJSymbol(const char* libName, const char* symbolName)
{
    if (libName == nullptr || symbolName == nullptr) {
        return nullptr;
    }

    MapleRuntime::Uptr symbol = MapleRuntime::LoaderManager::GetInstance()->FindSymbol(libName, symbolName);
    return reinterpret_cast<void*>(symbol);
}

int UnloadCJLibrary(const char* libName)
{
    // Runtime has not been initialized, report error and return.
    if (libName == nullptr) {
        return E_ARGS;
    }
    if (MapleRuntime::LoaderManager::GetInstance()->UnLoadLibrary(libName) == 0) {
        return E_OK;
    }
    return E_ARGS;
}

void RegisterStackInfoCallbacks(UpdateStackInfoFunc uFunc)
{
    CJRegisterStackInfoCallbacks(uFunc);
}

void RegisterArkVMInRuntime(unsigned long long vm)
{
    CJRegisterArkVMInRuntime(vm);
}

int CJThreadKeyCreate(CJThreadKey* key, DestructorFunc destructor)
{
#if defined(GENERAL_ASAN_SUPPORT_INTERFACE)
    MapleRuntime::Sanitizer::AsanRead(key, sizeof(CJThreadKey));
#endif
    return CJThreadKeyCreateInner(key, destructor);
}

int CJThreadSetspecific(CJThreadKey key, void* value) { return CJThreadSetspecificInner(key, value); }

#ifndef _WIN64
void AddHandlerToSignalStack(int signal, SignalAction* sa)
{
    MapleRuntime::SignalManager::AddHandlerToSignalStack(signal, sa);
}

void RemoveHandlerFromSignalStack(int signal, bool (*fn)(int, siginfo_t*, void*))
{
    MapleRuntime::SignalManager::RemoveHandlerFromSignalStack(signal, fn);
}
#endif

#if (defined(_WIN64))
void CJ_MCC_RegisterLogHandle(MapleRuntime::LogHandle handle) { MapleRuntime::Logger::RegisterLogHandle(handle); }
#endif

void* CJThreadGetspecific(CJThreadKey key) { return CJThreadGetspecificInner(key); }

void CJ_MRT_DumpHeapSnapshot(int fd)
{
    MapleRuntime::ScopedEnterSaferegion enterSaferegion(false);
    MapleRuntime::CjHeapData cjHeapData;
    cjHeapData.DumpHeap(fd);
}

void CJ_MRT_ForceFullGC() { MapleRuntime::HeapManager::RequestGC(MapleRuntime::GC_REASON_USER, false); }

CANGJIE_RT_API_DECLS_END
