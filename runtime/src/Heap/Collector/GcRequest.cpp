// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#include "GcRequest.h"

#include "Base/TimeUtils.h"
#include "GcStats.h"
namespace MapleRuntime {
namespace {
// Set a safe initial value so that the first GC is able to trigger.
uint64_t g_initHeuTriggerTimestamp = TimeUtil::NanoSeconds() - LONG_MIN_HEU_GC_INTERVAL_NS;
uint64_t g_initNativeTriggerTimestamp = TimeUtil::NanoSeconds() - MIN_ASYNC_GC_INTERVAL_NS;
} // namespace

inline bool GCRequest::IsFrequentGC() const
{
    if (minIntervelNs == 0) {
        return false;
    }
    int64_t now = static_cast<int64_t>(TimeUtil::NanoSeconds());
    return (now - prevRequestTime < minIntervelNs);
}

inline bool GCRequest::IsFrequentAsyncGC() const
{
    int64_t now = static_cast<int64_t>(TimeUtil::NanoSeconds());
    return (now - GCStats::GetPrevGCFinishTime() < minIntervelNs);
}

// heuristic gc is triggered by object allocation,
// the heap stats should take into consideration.
inline bool GCRequest::IsFrequentHeuristicGC() const { return IsFrequentAsyncGC(); }

bool GCRequest::ShouldBeIgnored() const
{
    switch (reason) {
        case GC_REASON_HEU:
            return IsFrequentHeuristicGC();
        case GC_REASON_NATIVE:
            return IsFrequentAsyncGC();
        case GC_REASON_OOM:
        case GC_REASON_FORCE:
            return IsFrequentGC();
        default:
            return false;
    }
}

GCRequest g_gcRequests[] = {
    { GC_REASON_USER, "user", false, true, 0, 0 },
    { GC_REASON_OOM, "oom", true, false, 0, 0 },
    { GC_REASON_BACKUP, "backup", true, false, 0, 0 },
    { GC_REASON_HEU, "heuristic", false, true, LONG_MIN_HEU_GC_INTERVAL_NS, g_initHeuTriggerTimestamp },
    { GC_REASON_NATIVE, "native_alloc", false, true, MIN_ASYNC_GC_INTERVAL_NS, g_initNativeTriggerTimestamp },
    { GC_REASON_HEU_SYNC, "heuristic_sync", true, true, 0, 0 },
    { GC_REASON_NATIVE_SYNC, "native_alloc_sync", true, true, 0, 0 },
    { GC_REASON_FORCE, "force", true, false, 0, 0 }
};
} // namespace MapleRuntime
