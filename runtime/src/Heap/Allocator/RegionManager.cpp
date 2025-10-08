// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#include "Allocator/RegionManager.h"

#include <cmath>
#include <unistd.h>

#include "Allocator/RegionSpace.h"
#include "Base/CString.h"
#include "Collector/Collector.h"
#include "Collector/CopyCollector.h"
#include "Common/ScopedObjectAccess.h"
#include "Heap.h"
#include "Mutator/Mutator.inline.h"
#include "Mutator/MutatorManager.h"
#include "ObjectModel/RefField.inline.h"
#if defined(CANGJIE_TSAN_SUPPORT)
#include "Sanitizer/SanitizerInterface.h"
#endif
#include "Sync/Sync.h"

namespace MapleRuntime {
uintptr_t RegionInfo::UnitInfo::totalUnitCount = 0;
uintptr_t RegionInfo::UnitInfo::unitInfoStart = 0;
uintptr_t RegionInfo::UnitInfo::heapStartAddress = 0;

static size_t GetPageSize() noexcept
{
#if defined(_WIN64)
    SYSTEM_INFO systeminfo;
    GetSystemInfo(&systeminfo);
    if (systeminfo.dwPageSize != 0) {
        return systeminfo.dwPageSize;
    } else {
        // default page size is 4KB if get system page size failed.
        return 4 * KB;
    }
#elif defined(__APPLE__)
    // default page size is 4KB in MacOS
    return 4 * KB;
#else
    return getpagesize();
#endif
}

// System default page size
const size_t MRT_PAGE_SIZE = GetPageSize();
const size_t AllocatorUtils::ALLOC_PAGE_SIZE = MapleRuntime::MRT_PAGE_SIZE;
// region unit size: same as system page size
const size_t RegionInfo::UNIT_SIZE = MapleRuntime::MRT_PAGE_SIZE;
// regarding a object as a large object when the size is greater than 32KB or one page size,
// depending on the system page size.
const size_t RegionInfo::LARGE_OBJECT_DEFAULT_THRESHOLD = MapleRuntime::MRT_PAGE_SIZE > (32 * KB) ?
                                                            MapleRuntime::MRT_PAGE_SIZE : 32 * KB;
// max size of per region is 128KB.
const size_t RegionManager::MAX_UNIT_COUNT_PER_REGION = (128 * KB) / MapleRuntime::MRT_PAGE_SIZE;
// size of huge page is 2048KB.
const size_t RegionManager::HUGE_PAGE = (2048 * KB) / MapleRuntime::MRT_PAGE_SIZE;;

class ForwardTask : public HeapWork {
public:
    ForwardTask(RegionManager& manager, RegionList& fromSpace)
        : regionManager(manager), fromRegionList(fromSpace) {}

    ~ForwardTask() = default;

    void Execute(size_t) override
    {
        while (true) {
            RegionInfo* region = fromRegionList.TakeHeadRegion();
            if (region == nullptr) { break; }
            region->SetRegionType(RegionInfo::RegionType::LONE_FROM_REGION);
            regionManager.ForwardRegion(region);
        }
    }

private:
    RegionManager& regionManager;
    RegionList& fromRegionList;
};

#if defined(GCINFO_DEBUG) && GCINFO_DEBUG
void RegionInfo::DumpRegionInfo(LogType type) const
{
    DLOG(type, "Region index: %zu, type: %s, address: 0x%zx-0x%zx, allocated(B) %zu, live(B) %zu", GetUnitIdx(),
         GetTypeName(), GetRegionStart(), GetRegionEnd(), GetRegionAllocatedSize(), GetLiveByteCount());
}

const char* RegionInfo::GetTypeName() const
{
    static constexpr const char* regionNames[] = {
        "undefined region",
        "thread local region",
        "recent fullregion",
        "from region",
        "unmovable from region",
        "to region",
        "full pinned region",
        "recent pinned region",
        "raw pointer pinned region",
        "tl raw pointer region",
        "large region",
        "recent large region",
        "garbage region",
    };
    return regionNames[static_cast<uint8_t>(GetRegionType())];
}
#endif

void RegionInfo::VisitAllObjects(const std::function<void(BaseObject*)>&& func)
{
    if (IsLargeRegion()) {
        func(reinterpret_cast<BaseObject*>(GetRegionStart()));
    } else if (IsSmallRegion()) {
        uintptr_t position = GetRegionStart();
        uintptr_t allocPtr = GetRegionAllocPtr();
        while (position < allocPtr) {
            // GetAllocSize should before call func, because object maybe destroy in compact gc.
            size_t size = RegionSpace::GetAllocSize(*reinterpret_cast<BaseObject*>(position));
            func(reinterpret_cast<BaseObject*>(position));
            position += size;
        }
    }
}

bool RegionInfo::VisitLiveObjectsUntilFalse(const std::function<bool(BaseObject*)>&& func)
{
    // no need to visit this region.
    if (GetLiveByteCount() == 0) {
        return true;
    }

    TracingCollector& collector = reinterpret_cast<TracingCollector&>(Heap::GetHeap().GetCollector());
    if (IsLargeRegion()) {
        return func(reinterpret_cast<BaseObject*>(GetRegionStart()));
    }
    if (IsSmallRegion()) {
        uintptr_t position = GetRegionStart();
        uintptr_t allocPtr = GetRegionAllocPtr();
        while (position < allocPtr) {
            BaseObject* obj = reinterpret_cast<BaseObject*>(position);
            position += RegionSpace::GetAllocSize(*obj);
            if (collector.IsSurvivedObject(obj) && !func(obj)) { return false; }
        }
    }
    return true;
}

void RegionList::MergeRegionList(RegionList& srcList, RegionInfo::RegionType regionType)
{
    RegionList regionList("region list cache");
    srcList.MoveTo(regionList);
    RegionInfo* head = regionList.GetHeadRegion();
    RegionInfo* tail = regionList.GetTailRegion();
    if (head == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(listMutex);
    regionList.SetElementType(regionType);
    IncCounts(regionList.GetRegionCount(), regionList.GetUnitCount());
    if (listHead == nullptr) {
        listHead = head;
        listTail = tail;
    } else {
        tail->SetNextRegion(listHead);
        listHead->SetPrevRegion(tail);
        listHead = head;
    }
}

void RegionList::PrependRegion(RegionInfo* region, RegionInfo::RegionType type)
{
    std::lock_guard<std::mutex> lock(listMutex);
    PrependRegionLocked(region, type);
}

void RegionList::PrependRegionLocked(RegionInfo* region, RegionInfo::RegionType type)
{
    if (region == nullptr) {
        return;
    }

    DLOG(REGION, "list %p (%zu, %zu)+(%zu, %zu) prepend region %p@[%#zx+%zu, %#zx) type %u->%u", this,
        regionCount, unitCount, 1llu, region->GetUnitCount(), region, region->GetRegionStart(),
        region->GetRegionAllocatedSize(), region->GetRegionEnd(), region->GetRegionType(), type);

    region->SetRegionType(type);
    region->SetPrevRegion(nullptr);
    IncCounts(1, region->GetUnitCount());
    region->SetNextRegion(listHead);
    if (listHead == nullptr) {
        MRT_ASSERT(listTail == nullptr, "PrependRegion listTail is not null");
        listTail = region;
    } else {
        listHead->SetPrevRegion(region);
    }
    listHead = region;
}

void RegionList::DeleteRegionLocked(RegionInfo* del)
{
    MRT_ASSERT(listHead != nullptr && listTail != nullptr, "illegal region list");

    RegionInfo* pre = del->GetPrevRegion();
    RegionInfo* next = del->GetNextRegion();

    del->SetNextRegion(nullptr);
    del->SetPrevRegion(nullptr);

    DLOG(REGION, "list %p (%zu, %zu)-(%zu, %zu) delete region %p@[%#zx+%zu, %#zx) type %u", this,
        regionCount, unitCount, 1llu, del->GetUnitCount(),
        del, del->GetRegionStart(), del->GetRegionAllocatedSize(), del->GetRegionEnd(), del->GetRegionType());

    DecCounts(1, del->GetUnitCount());

    if (listHead == del) { // delete head
        MRT_ASSERT(pre == nullptr, "Delete Region pre is not null");
        listHead = next;
        if (listHead == nullptr) { // now empty
            listTail = nullptr;
            return;
        }
    } else {
        pre->SetNextRegion(next);
    }

    if (listTail == del) { // delete tail
        MRT_ASSERT(next == nullptr, "Delete Region next is not null");
        listTail = pre;
        if (listTail == nullptr) { // now empty
            listHead = nullptr;
            return;
        }
    } else {
        next->SetPrevRegion(pre);
    }
}

#ifdef MRT_DEBUG
void RegionList::DumpRegionList(const char* msg)
{
    DLOG(REGION, "dump region list %s", msg);
    std::lock_guard<std::mutex> lock(listMutex);
    for (RegionInfo *region = listHead; region != nullptr; region = region->GetNextRegion()) {
        DLOG(REGION, "region %p @[0x%zx+%zu, 0x%zx) units [%zu+%zu, %zu) type %u prev %p next %p", region,
            region->GetRegionStart(), region->GetRegionAllocatedSize(), region->GetRegionEnd(),
            region->GetUnitIdx(), region->GetUnitCount(), region->GetUnitIdx() + region->GetUnitCount(),
            region->GetRegionType(), region->GetPrevRegion(), region->GetNextRegion());
    }
}
#endif
inline void RegionManager::TagHugePage(RegionInfo* region, size_t num) const
{
#if defined (__linux__) || defined(__OHOS__)
    (void)madvise(reinterpret_cast<void*>(region->GetRegionStart()), num * RegionInfo::UNIT_SIZE, MADV_HUGEPAGE);
#else
    (void)region;
    (void)num;
#endif
}

inline void RegionManager::UntagHugePage(RegionInfo* region, size_t num) const
{
#if defined (__linux__) || defined(__OHOS__)
    (void)madvise(reinterpret_cast<void*>(region->GetRegionStart()), num * RegionInfo::UNIT_SIZE, MADV_NOHUGEPAGE);
#else
    (void)region;
    (void)num;
#endif
}

size_t FreeRegionManager::ReleaseGarbageRegions(size_t targetCachedSize)
{
    size_t dirtyBytes = dirtyUnitTree.GetTotalCount() * RegionInfo::UNIT_SIZE;
    if (dirtyBytes <= targetCachedSize) {
        VLOG(REPORT, "release heap garbage memory 0 bytes, cache %zu(%zu) bytes", dirtyBytes, targetCachedSize);
        return 0;
    }

    size_t releasedBytes = 0;
    while (dirtyBytes > targetCachedSize) {
        std::lock_guard<std::mutex> lock1(dirtyUnitTreeMutex);
        auto node = dirtyUnitTree.RootNode();
        if (node == nullptr) { break; }
        Index idx = node->GetIndex();
        UnitCount num = node->GetCount();
        dirtyUnitTree.ReleaseRootNode();

        std::lock_guard<std::mutex> lock2(releasedUnitTreeMutex);
        CHECK_DETAIL(releasedUnitTree.MergeInsert(idx, num, true), "tid %d: failed to release garbage units[%u+%u, %u)",
                     GetTid(), idx, num, idx + num);
        releasedBytes += (num * RegionInfo::UNIT_SIZE);
        dirtyBytes = dirtyUnitTree.GetTotalCount() * RegionInfo::UNIT_SIZE;
    }
    VLOG(REPORT, "release heap garbage memory %zu bytes, cache %zu(%zu) bytes",
         releasedBytes, dirtyBytes, targetCachedSize);
    return releasedBytes;
}

void RegionManager::SetMaxUnitCountForRegion()
{
    maxUnitCountPerRegion = CangjieRuntime::GetHeapParam().regionSize * KB / RegionInfo::UNIT_SIZE;
}

void RegionManager::SetMaxUnitCountForPinnedRegion()
{
    auto env = std::getenv("cjPinnedRegionSize");
    if (env == nullptr) {
        maxUnitCountPerPinnedRegion = maxUnitCountPerRegion;
        return;
    }
    size_t size = CString::ParseSizeFromEnv(env);
    // The minimum region size is system page size, measured in KB.
    size_t minSize = MapleRuntime::MRT_PAGE_SIZE / KB;
    if (size >= minSize && size <= maxUnitCountPerRegion) {
        maxUnitCountPerPinnedRegion = size;
    } else {
        LOG(RTLOG_ERROR, "Unsupported cjPinnedRegionSize parameter. Valid cjPinnedRegionSize"
            "range is [%zuKB, %zuKB].\n", minSize, maxUnitCountPerRegion);
    }
}

void RegionManager::SetLargeObjectThreshold()
{
    auto env = std::getenv("cjLargeThresholdSize");
    if (env == nullptr) {
        // default value is 32 KB
        largeObjectThreshold = 32 * KB;
    }
    size_t size = CString::ParseSizeFromEnv(env);
    // The minimum region size is system page size, measured in KB.
    size_t minSize = MapleRuntime::MRT_PAGE_SIZE / KB;
    // 64UL: The maximum region size, measured in KB, the value is 2048 KB.
    size_t maxSize = 10 * 1024UL;
    if (size >= minSize && size <= maxSize) {
        largeObjectThreshold = size * KB;
    } else if (size != 0) {
        LOG(RTLOG_ERROR, "Unsupported cjLargeThresholdSize parameter. Valid cjLargeThresholdSize"
            "range is [%zuKB, 2048KB].\n", minSize);
    }
    size_t regionSize = CangjieRuntime::GetHeapParam().regionSize * KB;
    largeObjectThreshold = largeObjectThreshold > regionSize ? regionSize :  largeObjectThreshold;
}

void RegionManager::SetGarbageThreshold()
{
    fromSpaceGarbageThreshold = CangjieRuntime::GetGCParam().garbageThreshold;
}

void RegionManager::Initialize(size_t nUnit, uintptr_t regionInfoAddr)
{
    size_t metadataSize = GetMetadataSize(nUnit);
#ifdef _WIN64
    MemMap::CommitMemory(reinterpret_cast<void*>(regionInfoAddr), metadataSize);
#endif
    this->regionInfoStart = regionInfoAddr;
    this->regionHeapStart = regionInfoAddr + metadataSize;
    this->regionHeapEnd = regionHeapStart + nUnit * RegionInfo::UNIT_SIZE;
    this->inactiveZone = regionHeapStart;
    SetMaxUnitCountForRegion();
    SetMaxUnitCountForPinnedRegion();
    SetLargeObjectThreshold();
    SetGarbageThreshold();
    // propagate region heap layout
    RegionInfo::Initialize(nUnit, regionInfoAddr, regionHeapStart);
    freeRegionManager.Initialize(nUnit);
    this->exemptedRegionThreshold = CangjieRuntime::GetHeapParam().exemptionThreshold;

    DLOG(REPORT, "region info @0x%zx+%zu, heap [0x%zx, 0x%zx), unit count %zu", regionInfoAddr, metadataSize,
         regionHeapStart, regionHeapEnd, nUnit);
}

void RegionManager::ReclaimRegion(RegionInfo* region)
{
    size_t num = region->GetUnitCount();
    size_t unitIndex = region->GetUnitIdx();
    if (num >= HUGE_PAGE) {
        UntagHugePage(region, num);
    }
    DLOG(REGION, "reclaim region %p @[%#zx+%zu, %#zx) type %u", region, region->GetRegionStart(),
        region->GetRegionAllocatedSize(), region->GetRegionEnd(), region->GetRegionType());

    region->InitFreeUnits();
    freeRegionManager.AddGarbageUnits(unitIndex, num);
}

size_t RegionManager::ReleaseRegion(RegionInfo* region)
{
    size_t res = region->GetRegionSize();
    size_t num = region->GetUnitCount();
    size_t unitIndex = region->GetUnitIdx();
    if (num >= HUGE_PAGE) {
        UntagHugePage(region, num);
    }
    DLOG(REGION, "release region %p @[%#zx+%zu, %#zx) type %u", region, region->GetRegionStart(),
        region->GetRegionAllocatedSize(), region->GetRegionEnd(), region->GetRegionType());

    region->InitFreeUnits();
    RegionInfo::ReleaseUnits(unitIndex, num);
    freeRegionManager.AddReleaseUnits(unitIndex, num);
    return res;
}

void RegionManager::ReassembleFromSpace()
{
    fromRegionList.MergeRegionList(toRegionList, RegionInfo::RegionType::FROM_REGION);
    fromRegionList.MergeRegionList(unmovableFromRegionList, RegionInfo::RegionType::FROM_REGION);
}

void RegionManager::CountLiveObject(const BaseObject* obj)
{
    RegionInfo* region = RegionInfo::GetRegionInfoAt(reinterpret_cast<MAddress>(obj));
    region->AddLiveByteCount(obj->GetSize());
}

void RegionManager::AssembleSmallGarbageCandidates()
{
    fromRegionList.MergeRegionList(rawPointerPinnedRegionList, RegionInfo::RegionType::FROM_REGION);
    fromRegionList.MergeRegionList(rawPointerRegionList, RegionInfo::RegionType::FROM_REGION);
    fromRegionList.MergeRegionList(recentFullRegionList, RegionInfo::RegionType::FROM_REGION);
    fromRegionList.MergeRegionList(unmovableFromRegionList, RegionInfo::RegionType::FROM_REGION);

    fromRegionList.VisitAllRegions([](RegionInfo* region) { region->ClearLiveInfo(); });
}

void RegionManager::AssembleLargeGarbageCandidates()
{
    oldLargeRegionList.MergeRegionList(recentLargeRegionList, RegionInfo::RegionType::LARGE_REGION);
    for (RegionInfo* region = oldLargeRegionList.GetHeadRegion(); region != nullptr; region = region->GetNextRegion()) {
        region->ClearLiveInfo();
    }
}

void RegionManager::AssemblePinnedGarbageCandidates(bool collectAll)
{
    oldPinnedRegionList.MergeRegionList(recentPinnedRegionList, RegionInfo::RegionType::FULL_PINNED_REGION);
    RegionInfo* region = oldPinnedRegionList.GetHeadRegion();
    while (region != nullptr) {
        RegionInfo* nextRegion = region->GetNextRegion();
        if (collectAll && (region->GetRawPointerObjectCount() > 0)) {
            oldPinnedRegionList.DeleteRegion(region);
            rawPointerPinnedRegionList.PrependRegion(region, RegionInfo::RegionType::RAW_POINTER_PINNED_REGION);
        }
        region->ClearLiveInfo();
        region = nextRegion;
    }
}

// forward only regions whose garbage bytes is greater than or equal to exemptedRegionThreshold.
void RegionManager::ExemptFromRegions()
{
    size_t forwardBytes = 0;
    size_t floatingGarbage = 0;
    size_t oldFromBytes = fromRegionList.GetUnitCount() * RegionInfo::UNIT_SIZE;
    RegionInfo* fromRegion = fromRegionList.GetHeadRegion();
    while (fromRegion != nullptr) {
        size_t threshold = static_cast<size_t>(exemptedRegionThreshold * fromRegion->GetRegionSize());
        size_t liveBytes = fromRegion->GetLiveByteCount();
        long rawPtrCnt = fromRegion->GetRawPointerObjectCount();
        if (liveBytes > threshold) { // ignore this region
            RegionInfo* del = fromRegion;
            DLOG(REGION, "region %p @[0x%zx+%zu, 0x%zx) exempted by forwarding: %zu units, %u live bytes", del,
                del->GetRegionStart(), del->GetRegionAllocatedSize(), del->GetRegionEnd(),
                del->GetUnitCount(), del->GetLiveByteCount());

            fromRegion = fromRegion->GetNextRegion();
            if (fromRegionList.TryDeleteRegion(del, RegionInfo::RegionType::FROM_REGION,
                                               RegionInfo::RegionType::UNMOVABLE_FROM_REGION)) {
                ExemptFromRegion(del);
            }
            floatingGarbage += (del->GetRegionSize() - del->GetLiveByteCount());
        } else if (rawPtrCnt > 0) {
            RegionInfo* del = fromRegion;
            DLOG(REGION, "region %p @[0x%zx+%zu, 0x%zx) pinned by forwarding: %zu units, %u live bytes rawPtr cnt %u",
                del, del->GetRegionStart(), del->GetRegionAllocatedSize(), del->GetRegionEnd(),
                del->GetUnitCount(), del->GetLiveByteCount(), rawPtrCnt);

            fromRegion = fromRegion->GetNextRegion();
            if (fromRegionList.TryDeleteRegion(del, RegionInfo::RegionType::FROM_REGION,
                                               RegionInfo::RegionType::RAW_POINTER_PINNED_REGION)) {
                rawPointerPinnedRegionList.PrependRegion(del, RegionInfo::RegionType::RAW_POINTER_PINNED_REGION);
            }
            floatingGarbage += (del->GetRegionSize() - del->GetLiveByteCount());
        } else {
            forwardBytes += fromRegion->GetLiveByteCount();
            fromRegion = fromRegion->GetNextRegion();
        }
    }

    size_t newFromBytes = fromRegionList.GetUnitCount() * RegionInfo::UNIT_SIZE;
    size_t exemptedFromBytes = unmovableFromRegionList.GetUnitCount() * RegionInfo::UNIT_SIZE;
    VLOG(REPORT, "exempt from-space: %zu B - %zu B -> %zu B, %zu B floating garbage, %zu B to forward",
         oldFromBytes, exemptedFromBytes, newFromBytes, floatingGarbage, forwardBytes);
}

void RegionManager::ForEachObjUnsafe(const std::function<void(BaseObject*)>& visitor) const
{
    for (uintptr_t regionAddr = regionHeapStart; regionAddr < inactiveZone;) {
        RegionInfo* region = RegionInfo::GetRegionInfoAt(regionAddr);
        regionAddr = region->GetRegionEnd();
        if (!region->IsValidRegion() || region->IsFreeRegion() || region -> IsGarbageRegion()) {
            continue;
        }
        region->VisitAllObjects([&visitor](BaseObject* object) { visitor(object); });
    }
}

void RegionManager::ForEachObjSafe(const std::function<void(BaseObject*)>& visitor) const
{
    ScopedEnterSaferegion enterSaferegion(false);
    ScopedStopTheWorld stw("visit all objects");
    ForEachObjUnsafe(visitor);
}

RegionInfo* RegionManager::TakeRegion(size_t num, RegionInfo::UnitRole type, bool expectPhysicalMem)
{
    // a chance to invoke heuristic gc.
    if (!Heap::GetHeap().IsGcStarted()) {
        Collector& collector = Heap::GetHeap().GetCollector();
        size_t threshold = collector.GetGCStats().GetThreshold();
        size_t allocated = Heap::GetHeap().GetAllocator().AllocatedBytes();
        if (allocated >= threshold) {
            DLOG(ALLOC, "request heu gc: allocated %zu, threshold %zu", allocated, threshold);
            collector.RequestGC(GC_REASON_HEU, true);
        }
    }

    // check for allocation since we do not want gc threads and mutators do any harm to each other.
    size_t size = num * RegionInfo::UNIT_SIZE;
    RequestForRegion(size);

    RegionInfo* head = garbageRegionList.TakeHeadRegion();
    if (head != nullptr) {
        DLOG(REGION, "take garbage region %p@[%#zx, %#zx)", head, head->GetRegionStart(), head->GetRegionEnd());
        if (head->GetUnitCount() == num) {
            auto idx = head->GetUnitIdx();
            RegionInfo::ClearUnits(idx, num);
            DLOG(REGION, "reuse garbage region %p@[%#zx, %#zx)", head, head->GetRegionStart(), head->GetRegionEnd());
            return RegionInfo::InitRegion(idx, num, type);
        } else {
            DLOG(REGION, "reclaim garbage region %p@[%#zx, %#zx)", head, head->GetRegionStart(), head->GetRegionEnd());
            ReclaimRegion(head);
        }
    }

    RegionInfo* region = freeRegionManager.TakeRegion(num, type, expectPhysicalMem);
    if (region != nullptr) {
        if (num >= HUGE_PAGE) {
            TagHugePage(region, num);
        }
        return region;
    }

    // when free regions are not enough for allocation
    if (num <= GetInactiveUnitCount()) {
        uintptr_t addr = inactiveZone.fetch_add(size);
        if (addr < regionHeapEnd - size) {
            region = RegionInfo::InitRegionAt(addr, num, type);
            size_t idx = region->GetUnitIdx();
#ifdef _WIN64
            MemMap::CommitMemory(
                reinterpret_cast<void*>(RegionInfo::GetUnitAddress(idx)), num * RegionInfo::UNIT_SIZE);
#endif
            (void)idx; // eliminate compilation warning
            DLOG(REGION, "take inactive units [%zu+%zu, %zu) at [0x%zx, 0x%zx)", idx, num, idx + num,
                 RegionInfo::GetUnitAddress(idx), RegionInfo::GetUnitAddress(idx + num));
            if (num >= HUGE_PAGE) {
                TagHugePage(region, num);
            }
            if (expectPhysicalMem) {
                RegionInfo::ClearUnits(idx, num);
            }
            return region;
        } else {
            (void)inactiveZone.fetch_sub(size);
        }
    }

    return nullptr;
}

void RegionManager::ForwardFromRegions(GCThreadPool* threadPool)
{
    if (threadPool != nullptr) {
        int32_t threadNum = threadPool->GetMaxThreadNum() + 1;
        // We won't change fromRegionList during gc, so we can use it without lock.
        size_t regionCount = fromRegionList.GetRegionCount();
        if (UNLIKELY(regionCount == 0)) {
            return;
        }

        // we start threadPool before adding work so that we can concurrently add tasks;
        threadPool->Start();
        for (int32_t i = 0; i < threadNum; ++i) {
            threadPool->AddWork(new (std::nothrow) ForwardTask(*this, fromRegionList));
        }
        threadPool->WaitFinish();
    } else {
        ForwardFromRegions();
    }
}

void RegionManager::ExemptFromRegion(RegionInfo* region)
{
    unmovableFromRegionList.PrependRegion(region, RegionInfo::RegionType::UNMOVABLE_FROM_REGION);
}

void RegionManager::ForwardFromRegions()
{
    RegionInfo* fromRegion = fromRegionList.GetHeadRegion();
    while (fromRegion != nullptr) {
        MRT_ASSERT(fromRegion->IsValidRegion(), "the head region of fromRegionList is invalid");
        RegionInfo* region = fromRegion;
        fromRegion = fromRegion->GetNextRegion();
        ForwardRegion(region);
    }

    VLOG(REPORT, "forward %zu from-region units", fromRegionList.GetUnitCount());

    AllocBuffer* allocBuffer = AllocBuffer::GetAllocBuffer();
    if (LIKELY(allocBuffer != nullptr)) {
        allocBuffer->ClearRegion(); // clear region for next GC
    }
}

void RegionManager::CollectFreePinnedSlots(RegionInfo* region)
{
    // traverse pinned region to reclaim free pinned objects.
    TracingCollector& collector = reinterpret_cast<TracingCollector&>(Heap::GetHeap().GetCollector());
    region->VisitAllObjects([this, &collector](BaseObject* object) {
        if (!collector.IsSurvivedObject(object)) {
            DLOG(ALLOC, "reclaim pinned obj %p<%p>(%zu)", object, object->GetTypeInfo(), object->GetSize());
            std::lock_guard<std::mutex> lock(freePinnedSlotListMutex);
            ReleaseNativeResource(object);
            freePinnedSlotLists.PushFront(object);
        }
    });
}

size_t RegionManager::CollectPinnedGarbage()
{
    {
        std::lock_guard<std::mutex> lock(freePinnedSlotListMutex);
        freePinnedSlotLists.Clear();
    }
    size_t garbageSize = 0;
    RegionInfo* region = oldPinnedRegionList.GetHeadRegion();
    while (region != nullptr) {
        if (region->GetLiveByteCount() == 0) {
            RegionInfo* del = region;
            region = region->GetNextRegion();
            oldPinnedRegionList.DeleteRegion(del);

            auto fixToObj = [](BaseObject* obj) { ReleaseNativeResource(obj); };
            del->VisitAllObjects(fixToObj);

            garbageSize += CollectRegion(del);
            continue;
        } else {
            CollectFreePinnedSlots(region);
            region = region->GetNextRegion();
        }
    }
    return garbageSize;
}

size_t RegionManager::CollectLargeGarbage()
{
    size_t garbageSize = 0;
    TracingCollector& collector = reinterpret_cast<TracingCollector&>(Heap::GetHeap().GetCollector());
    RegionInfo* region = oldLargeRegionList.GetHeadRegion();
    while (region != nullptr) {
        MAddress addr = region->GetRegionStart();
        BaseObject* obj = reinterpret_cast<BaseObject*>(addr);
        if (!collector.IsSurvivedObject(obj)) {
            DLOG(REGION, "reclaim large region %p@[0x%zx+%zu, 0x%zx) type %u", region, region->GetRegionStart(),
                 region->GetRegionAllocatedSize(), region->GetRegionEnd(), region->GetRegionType());

            RegionInfo* del = region;
            region = region->GetNextRegion();
            oldLargeRegionList.DeleteRegion(del);
            if (del->GetRegionSize() > RegionInfo::LARGE_OBJECT_RELEASE_THRESHOLD) {
                garbageSize += ReleaseRegion(del);
            } else {
                garbageSize += CollectRegion(del);
            }
        } else {
            region->ResetMarkBit();
            region = region->GetNextRegion();
        }
    }

    region = recentLargeRegionList.GetHeadRegion();
    while (region != nullptr) {
        region->ResetMarkBit();
        region = region->GetNextRegion();
    }

    return garbageSize;
}

#if defined(GCINFO_DEBUG) && GCINFO_DEBUG
void RegionManager::DumpRegionInfo() const
{
    if (!ENABLE_LOG(ALLOC)) {
        return;
    }
    for (uintptr_t regionAddr = regionHeapStart; regionAddr < inactiveZone;) {
        RegionInfo* region = RegionInfo::GetRegionInfoAt(regionAddr);
        regionAddr = region->GetRegionEnd();
        if (!region->IsFreeRegion()) {
            region->DumpRegionInfo(ALLOC);
        }
    }
}
#endif

void RegionManager::DumpRegionStats(const char* msg) const
{
    size_t totalSize = regionHeapEnd - regionHeapStart;
    size_t totalUnits = totalSize / RegionInfo::UNIT_SIZE;
    size_t activeSize = inactiveZone - regionHeapStart;
    size_t activeUnits = activeSize / RegionInfo::UNIT_SIZE;

    size_t tlRegions = tlRegionList.GetRegionCount();
    size_t tlUnits = tlRegionList.GetUnitCount();
    size_t tlSize = tlUnits * RegionInfo::UNIT_SIZE;
    size_t allocTLSize = tlRegionList.GetAllocatedSize();

    size_t fromRegions = fromRegionList.GetRegionCount();
    size_t fromUnits = fromRegionList.GetUnitCount();
    size_t fromSize = fromUnits * RegionInfo::UNIT_SIZE;
    size_t allocFromSize = fromRegionList.GetAllocatedSize();

    size_t exemptedFromRegions = unmovableFromRegionList.GetRegionCount();
    size_t exemptedFromUnits = unmovableFromRegionList.GetUnitCount();
    size_t exemptedFromSize = exemptedFromUnits * RegionInfo::UNIT_SIZE;
    size_t allocExemptedFromSize = unmovableFromRegionList.GetAllocatedSize();

    size_t toRegions = toRegionList.GetRegionCount();
    size_t toUnits = toRegionList.GetUnitCount();
    size_t toSize = toUnits * RegionInfo::UNIT_SIZE;
    size_t allocToSize = toRegionList.GetAllocatedSize();

    size_t recentFullRegions = recentFullRegionList.GetRegionCount();
    size_t recentFullUnits = recentFullRegionList.GetUnitCount();
    size_t recentFullSize = recentFullUnits * RegionInfo::UNIT_SIZE;
    size_t allocRecentFullSize = recentFullRegionList.GetAllocatedSize();

    size_t garbageRegions = garbageRegionList.GetRegionCount();
    size_t garbageUnits = garbageRegionList.GetUnitCount();
    size_t garbageSize = garbageUnits * RegionInfo::UNIT_SIZE;
    size_t allocGarbageSize = garbageRegionList.GetAllocatedSize();

    size_t pinnedRegions = oldPinnedRegionList.GetRegionCount();
    size_t pinnedUnits = oldPinnedRegionList.GetUnitCount();
    size_t pinnedSize = pinnedUnits * RegionInfo::UNIT_SIZE;
    size_t allocPinnedSize = oldPinnedRegionList.GetAllocatedSize();

    size_t recentPinnedRegions = recentPinnedRegionList.GetRegionCount();
    size_t recentPinnedUnits = recentPinnedRegionList.GetUnitCount();
    size_t recentPinnedSize = recentPinnedUnits * RegionInfo::UNIT_SIZE;
    size_t allocRecentPinnedSize = recentPinnedRegionList.GetAllocatedSize();

    size_t largeRegions = oldLargeRegionList.GetRegionCount();
    size_t largeUnits = oldLargeRegionList.GetUnitCount();
    size_t largeSize = largeUnits * RegionInfo::UNIT_SIZE;
    size_t allocLargeSize = oldLargeRegionList.GetAllocatedSize();

    size_t recentlargeRegions = recentLargeRegionList.GetRegionCount();
    size_t recentlargeUnits = recentLargeRegionList.GetUnitCount();
    size_t recentLargeSize = recentlargeUnits * RegionInfo::UNIT_SIZE;
    size_t allocRecentLargeSize = recentLargeRegionList.GetAllocatedSize();

    size_t usedUnits = GetUsedUnitCount();
    size_t releasedUnits = freeRegionManager.GetReleasedUnitCount();
    size_t dirtyUnits = freeRegionManager.GetDirtyUnitCount();
    size_t listedUnits = fromUnits + exemptedFromUnits + toUnits + garbageUnits +
        recentFullUnits + largeUnits + recentlargeUnits + pinnedUnits + recentPinnedUnits;

    VLOG(REPORT, msg);

    VLOG(REPORT, "\ttotal units: %zu (%zu B)", totalUnits, totalSize);
    VLOG(REPORT, "\tactive units: %zu (%zu B)", activeUnits, activeSize);

    VLOG(REPORT, "\ttl-regions %zu: %zu units (%zu B, alloc %zu)", tlRegions,  tlUnits, tlSize, allocTLSize);
    VLOG(REPORT, "\tfrom-regions %zu: %zu units (%zu B, alloc %zu)", fromRegions,  fromUnits, fromSize, allocFromSize);
    VLOG(REPORT, "\texempted from-regions %zu: %zu units (%zu B, alloc %zu)",
         exemptedFromRegions, exemptedFromUnits, exemptedFromSize, allocExemptedFromSize);
    VLOG(REPORT, "\tto-regions %zu: %zu units (%zu B, alloc %zu)", toRegions, toUnits, toSize, allocToSize);
    VLOG(REPORT, "\trecent-full regions %zu: %zu units (%zu B, alloc %zu)",
         recentFullRegions, recentFullUnits, recentFullSize, allocRecentFullSize);
    VLOG(REPORT, "\tgarbage regions %zu: %zu units (%zu B, alloc %zu)",
         garbageRegions, garbageUnits, garbageSize, allocGarbageSize);
    VLOG(REPORT, "\tpinned regions %zu: %zu units (%zu B, alloc %zu)",
         pinnedRegions, pinnedUnits, pinnedSize, allocPinnedSize);
    VLOG(REPORT, "\trecent pinned regions %zu: %zu units (%zu B, alloc %zu)",
         recentPinnedRegions, recentPinnedUnits, recentPinnedSize, allocRecentPinnedSize);
    VLOG(REPORT, "\tlarge-object regions %zu: %zu units (%zu B, alloc %zu)",
         largeRegions, largeUnits, largeSize, allocLargeSize);
    VLOG(REPORT, "\trecent large-object regions %zu: %zu units (%zu B, alloc %zu)",
         recentlargeRegions, recentlargeUnits, recentLargeSize, allocRecentLargeSize);

    VLOG(REPORT, "\tlisted units: %zu (%zu B)", listedUnits, listedUnits * RegionInfo::UNIT_SIZE);
    VLOG(REPORT, "\tused units: %zu (%zu B)", usedUnits, usedUnits * RegionInfo::UNIT_SIZE);
    VLOG(REPORT, "\treleased units: %zu (%zu B)", releasedUnits, releasedUnits * RegionInfo::UNIT_SIZE);
    VLOG(REPORT, "\tdirty units: %zu (%zu B)", dirtyUnits, dirtyUnits * RegionInfo::UNIT_SIZE);

    OHOS_HITRACE_COUNT("CJRT_GC_totalSize", totalSize);
    OHOS_HITRACE_COUNT("CJRT_GC_totalUnits", totalUnits);
    OHOS_HITRACE_COUNT("CJRT_GC_activeSize", activeSize);
    OHOS_HITRACE_COUNT("CJRT_GC_activeUnits", activeUnits);
    OHOS_HITRACE_COUNT("CJRT_GC_tlRegions", tlRegions);
    OHOS_HITRACE_COUNT("CJRT_GC_tlUnits", tlUnits);
    OHOS_HITRACE_COUNT("CJRT_GC_tlSize", tlSize);
    OHOS_HITRACE_COUNT("CJRT_GC_allocTLSize", allocTLSize);
    OHOS_HITRACE_COUNT("CJRT_GC_fromRegions", fromRegions);
    OHOS_HITRACE_COUNT("CJRT_GC_fromUnits", fromUnits);
    OHOS_HITRACE_COUNT("CJRT_GC_fromSize", fromSize);
    OHOS_HITRACE_COUNT("CJRT_GC_allocFromSize", allocFromSize);
    OHOS_HITRACE_COUNT("CJRT_GC_exemptedFromRegions", exemptedFromRegions);
    OHOS_HITRACE_COUNT("CJRT_GC_exemptedFromUnits", exemptedFromUnits);
    OHOS_HITRACE_COUNT("CJRT_GC_exemptedFromSize", exemptedFromSize);
    OHOS_HITRACE_COUNT("CJRT_GC_allocExemptedFromSize", allocExemptedFromSize);
    OHOS_HITRACE_COUNT("CJRT_GC_toRegions", toRegions);
    OHOS_HITRACE_COUNT("CJRT_GC_toUnits", toUnits);
    OHOS_HITRACE_COUNT("CJRT_GC_toSize", toSize);
    OHOS_HITRACE_COUNT("CJRT_GC_allocToSize", allocToSize);
    OHOS_HITRACE_COUNT("CJRT_GC_recentFullRegions", recentFullRegions);
    OHOS_HITRACE_COUNT("CJRT_GC_recentFullUnits", recentFullUnits);
    OHOS_HITRACE_COUNT("CJRT_GC_recentFullSize", recentFullSize);
    OHOS_HITRACE_COUNT("CJRT_GC_allocRecentFullSize", allocRecentFullSize);
    OHOS_HITRACE_COUNT("CJRT_GC_garbageRegions", garbageRegions);
    OHOS_HITRACE_COUNT("CJRT_GC_garbageUnits", garbageUnits);
    OHOS_HITRACE_COUNT("CJRT_GC_garbageSize", garbageSize);
    OHOS_HITRACE_COUNT("CJRT_GC_allocGarbageSize", allocGarbageSize);
    OHOS_HITRACE_COUNT("CJRT_GC_pinnedRegions", pinnedRegions);
    OHOS_HITRACE_COUNT("CJRT_GC_pinnedUnits", pinnedUnits);
    OHOS_HITRACE_COUNT("CJRT_GC_pinnedSize", pinnedSize);
    OHOS_HITRACE_COUNT("CJRT_GC_allocPinnedSize", allocPinnedSize);
    OHOS_HITRACE_COUNT("CJRT_GC_recentPinnedRegions", recentPinnedRegions);
    OHOS_HITRACE_COUNT("CJRT_GC_recentPinnedUnits", recentPinnedUnits);
    OHOS_HITRACE_COUNT("CJRT_GC_recentPinnedSize", recentPinnedSize);
    OHOS_HITRACE_COUNT("CJRT_GC_allocRecentPinnedSize", allocRecentPinnedSize);
    OHOS_HITRACE_COUNT("CJRT_GC_largeRegions", largeRegions);
    OHOS_HITRACE_COUNT("CJRT_GC_largeUnits", largeUnits);
    OHOS_HITRACE_COUNT("CJRT_GC_largeSize", largeSize);
    OHOS_HITRACE_COUNT("CJRT_GC_allocLargeSize", allocLargeSize);
    OHOS_HITRACE_COUNT("CJRT_GC_recentlargeRegions", recentlargeRegions);
    OHOS_HITRACE_COUNT("CJRT_GC_recentlargeUnits", recentlargeUnits);
    OHOS_HITRACE_COUNT("CJRT_GC_recentLargeSize", recentLargeSize);
    OHOS_HITRACE_COUNT("CJRT_GC_allocRecentLargeSize", allocRecentLargeSize);
    OHOS_HITRACE_COUNT("CJRT_GC_usedUnits", usedUnits);
    OHOS_HITRACE_COUNT("CJRT_GC_releasedUnits", releasedUnits);
    OHOS_HITRACE_COUNT("CJRT_GC_dirtyUnits", dirtyUnits);
    OHOS_HITRACE_COUNT("CJRT_GC_listedUnits", listedUnits);
}

RegionInfo* RegionManager::AllocateThreadLocalRegion(bool expectPhysicalMem)
{
    RegionInfo* region = TakeRegion(maxUnitCountPerRegion, RegionInfo::UnitRole::SMALL_SIZED_UNITS, expectPhysicalMem);
    if (region != nullptr) {
        {
            GCPhase phase = Heap::GetHeap().GetCollector().GetGCPhase();
            if (phase == GC_PHASE_TRACE || phase == GC_PHASE_CLEAR_SATB_BUFFER) {
                region->SetTraceRegionFlag(1);
            }
            tlRegionList.PrependRegion(region, RegionInfo::RegionType::THREAD_LOCAL_REGION);
            DLOG(REGION, "alloc tl-region %p @[0x%zx+%zu, 0x%zx) units[%zu+%zu, %zu) type %u",
                region, region->GetRegionStart(), region->GetRegionSize(), region->GetRegionEnd(),
                region->GetUnitIdx(), region->GetUnitCount(), region->GetUnitIdx() + region->GetUnitCount(),
                region->GetRegionType());
        }
    }

    return region;
}

void RegionManager::RequestForRegion(size_t size)
{
    if (IsGcThread()) {
        // gc thread is always permitted for allocation.
        return;
    }

    Heap& heap = Heap::GetHeap();
    GCStats& gcstats = heap.GetCollector().GetGCStats();
    size_t allocatedBytes = GetAllocatedSize() - gcstats.liveBytesAfterGC;
    constexpr double pi = 3.14;
    size_t availableBytesAfterGC = heap.GetMaxCapacity() - gcstats.liveBytesAfterGC;
    double heuAllocRate = std::cos((pi / 2.0) * allocatedBytes / availableBytesAfterGC) * gcstats.collectionRate;
    // for maximum performance, choose the larger one.
    double allocRate = std::max(
        static_cast<double>(CangjieRuntime::GetHeapParam().allocationRate) * MB / SECOND_TO_NANO_SECOND, heuAllocRate);
    size_t waitTime = static_cast<size_t>(size / allocRate);
    uint64_t now = TimeUtil::NanoSeconds();
    if (prevRegionAllocTime + waitTime <= now) {
        prevRegionAllocTime = TimeUtil::NanoSeconds();
        return;
    }

    uint64_t sleepTime = std::min<uint64_t>(CangjieRuntime::GetHeapParam().allocationWaitTime,
                                  prevRegionAllocTime + waitTime - now);
    DLOG(ALLOC, "wait %zu ns to alloc %zu(B)", sleepTime, size);
    std::this_thread::sleep_for(std::chrono::nanoseconds{ sleepTime });
    prevRegionAllocTime = TimeUtil::NanoSeconds();
}

bool RegionManager::RouteOrCompactRegionImpl(RegionInfo* region)
{
    CHECK(region->IsRoutingState());
    CHECK_DETAIL(region->GetRawPointerObjectCount() <= 0, "pinned region shouldn't be moved");
    size_t fromBytes = region->GetLiveByteCount();
    AllocBuffer* buffer = AllocBuffer::GetOrCreateAllocBuffer();
    RegionInfo* toRegion1 = buffer->GetRegion();
    CHECK(region != toRegion1);
    bool result;
    if (toRegion1 == RegionInfo::NullRegion()) {
        toRegion1 = AllocateThreadLocalRegion();
        if (toRegion1 == nullptr) {
            CompactRegion(region);
            toRegion1 = region;
            result = false;
        } else {
            toRegion1->Alloc(fromBytes);
            result = true;
        }
        buffer->SetRegion(toRegion1);
        region->SetRouteInfo(toRegion1->GetRegionStart(), fromBytes);
        DLOG(FORWARD, "route region %p@[%#zx+%zu, %#zx) => %p@[%#zx~%#zx, %#zx)",
            region, region->GetRegionStart(), fromBytes, region->GetRegionEnd(), toRegion1,
            toRegion1->GetRegionStart(), toRegion1->GetRegionStart() + fromBytes, toRegion1->GetRegionEnd());
        return result;
    }

    size_t toRegion1Capacity = toRegion1->GetAvailableSize();
    MAddress toRegion1Addr = toRegion1->GetRegionAllocPtr();
    if (fromBytes <= toRegion1Capacity) {
        toRegion1->Alloc(fromBytes);
        region->SetRouteInfo(toRegion1Addr, fromBytes);
        DLOG(FORWARD, "route region %p@[%#zx+%zu, %#zx) => %p@[%#zx, %#zx~%#zx, %#zx)",
            region, region->GetRegionStart(), fromBytes, region->GetRegionEnd(), toRegion1,
            toRegion1->GetRegionStart(), toRegion1Addr, toRegion1Addr + fromBytes, toRegion1->GetRegionEnd());
        return true;
    }
    size_t toRegion1Waste = toRegion1Capacity;
    BaseObject* leftObject = nullptr;
    (void)region->VisitLiveObjectsUntilFalse([&toRegion1Waste, &leftObject](BaseObject* obj) {
        size_t objSz = RegionSpace::GetAllocSize(*obj);
        if (toRegion1Waste >= objSz) {
            toRegion1Waste -= objSz;
            return true;
        } else {
            leftObject = obj;
            return false;
        }
    });
    MAddress usedBytes1 = toRegion1Capacity - toRegion1Waste;
    MAddress usedBytes2 = fromBytes - usedBytes1;
    CHECK(toRegion1->IsThreadLocalRegion());
    {
        RemoveThreadLocalRegion(toRegion1);
        EnlistFullThreadLocalRegion(toRegion1);
    }

    RegionInfo* toRegion2 = AllocateThreadLocalRegion();
    CHECK(region != toRegion2);
    if (toRegion2 != nullptr) {
        toRegion1->Alloc(usedBytes1);
        CHECK(toRegion2->Alloc(usedBytes2) != 0);
        result = true;
    } else {
        CompactRegion(region, toRegion1);
        toRegion2 = region; // region is partially compacted into itself.
        result = false;
    }
    buffer->SetRegion(toRegion2);
    uint32_t toRegion2Idx = toRegion2->GetUnitIdx();
    region->SetRouteInfo(toRegion1Addr, usedBytes1, toRegion2Idx);
    DLOG(FORWARD, "route region %p@[%#zx+%zu, %#zx) => %p@[%#zx, %#zx~%#zx, %#zx) & %p@[%#zx~%#zx, %#zx)", region,
        region->GetRegionStart(), fromBytes, region->GetRegionEnd(), toRegion1, toRegion1->GetRegionStart(),
        toRegion1Addr, toRegion1Addr + usedBytes1, toRegion1->GetRegionEnd(), toRegion2,
        toRegion2->GetRegionStart(), toRegion2->GetRegionStart() + usedBytes2, toRegion2->GetRegionEnd());
    return result;
}

void RegionManager::CompactRegion(RegionInfo* region)
{
    DLOG(REGION, "compact region %p@[%#zx+%zu, %#zx) type %u", region, region->GetRegionStart(),
        region->GetLiveByteCount(), region->GetRegionEnd(), region->GetRegionType());

    MAddress regionStart = region->GetRegionStart();
    MAddress regionLimit = region->GetRegionAllocPtr();
    region->SetRegionAllocPtr(regionStart);
    CopyCollector& collector = reinterpret_cast<CopyCollector&>(Heap::GetHeap().GetCollector());
    for (MAddress currentPtr = regionStart; currentPtr < regionLimit;) {
        BaseObject* currentObj = reinterpret_cast<BaseObject*>(currentPtr);
        size_t size = currentObj->GetSize();
        if (region->IsMarkedObject(currentObj) || region->IsResurrectedObject(currentObj)) {
            MAddress toAddress = region->Alloc(size);
            BaseObject* toObj = reinterpret_cast<BaseObject*>(toAddress);
            DLOG(FORWARD, "compact obj %p<%p>(%zu) to %p", currentObj, currentObj->GetTypeInfo(), size, toObj);
            collector.CopyObject(*currentObj, *toObj, size);
            toObj->SetStateCode(ObjectState::NORMAL);
        }
        currentPtr += size;
    }
    std::atomic_thread_fence(std::memory_order_release);

    // clear unused space which is free after compaction.
    MAddress cur = region->GetRegionAllocPtr();
    if (regionLimit > cur) {
        size_t reclaimSize = regionLimit - cur;
        CHECK_DETAIL(memset_s(reinterpret_cast<void*>(cur), reclaimSize, 0, reclaimSize) == EOK, "clear buffer failed");
    }

    if (region->IsFromRegion()) {
        fromRegionList.DeleteRegion(region);
    }
    tlRegionList.PrependRegion(region, RegionInfo::RegionType::THREAD_LOCAL_REGION);
}

void RegionManager::CompactRegion(RegionInfo* region, RegionInfo* toRegion1)
{
    DLOG(REGION, "compact region %p@[%#zx+%zu, %#zx) type %u to region %p@%#zx:%#zx",
        region, region->GetRegionStart(), region->GetLiveByteCount(), region->GetRegionEnd(), region->GetRegionType(),
        toRegion1, toRegion1->GetRegionStart(), toRegion1->GetRegionAllocPtr());
    MAddress currentPtr = region->GetRegionStart();
    BaseObject* currentObj = reinterpret_cast<BaseObject*>(currentPtr);
    CopyCollector& collector = reinterpret_cast<CopyCollector&>(Heap::GetHeap().GetCollector());
    while (true) {
        size_t size = currentObj->GetSize();
        if (region->IsMarkedObject(currentObj) || region->IsResurrectedObject(currentObj)) {
            MAddress toAddress = toRegion1->Alloc(size);
            if (toAddress == 0) {
                break;
            }
            BaseObject* toObj = reinterpret_cast<BaseObject*>(toAddress);
            DLOG(FORWARD, "compact obj %p<%p>(%zu) to %p", currentObj, currentObj->GetTypeInfo(), size, toObj);
            collector.CopyObject(*currentObj, *toObj, size);
            toObj->SetStateCode(ObjectState::NORMAL);
            std::atomic_thread_fence(std::memory_order_release);
        }
        currentPtr += size;
        currentObj = reinterpret_cast<BaseObject*>(currentPtr);
    };

    MAddress regionLimit = region->GetRegionAllocPtr();
    region->SetRegionAllocPtr(region->GetRegionStart());
    while (currentPtr < regionLimit) {
        BaseObject* currentObj = reinterpret_cast<BaseObject*>(currentPtr);
        size_t size = currentObj->GetSize();
        if (region->IsMarkedObject(currentObj) || region->IsResurrectedObject(currentObj)) {
            MAddress toAddress = region->Alloc(size);
            BaseObject* toObj = reinterpret_cast<BaseObject*>(toAddress);
            DLOG(FORWARD, "compact obj %p<%p>(%zu) to %p", currentObj, currentObj->GetTypeInfo(), size, toObj);
            collector.CopyObject(*currentObj, *toObj, size);
            toObj->SetStateCode(ObjectState::NORMAL);
            std::atomic_thread_fence(std::memory_order_release);
        }
        currentPtr += size;
    }

    // clear unused space which is free after compaction.
    MAddress cur = region->GetRegionAllocPtr();
    if (regionLimit > cur) {
        size_t reclaimSize = regionLimit - cur;
        CHECK_DETAIL(memset_s(reinterpret_cast<void*>(cur), reclaimSize, 0, reclaimSize) == EOK, "clear buffer failed");
    }

    if (region->IsFromRegion()) {
        fromRegionList.DeleteRegion(region);
    }
    tlRegionList.PrependRegion(region, RegionInfo::RegionType::THREAD_LOCAL_REGION);
}

void RegionManager::ForwardRegion(RegionInfo* region)
{
    CHECK_DETAIL(region->IsFromRegion() || region->IsLoneFromRegion(), "region type %u", region->GetRegionType());

    DLOG(FORWARD, "try forward region %p @[0x%zx+%zu, 0x%zx) type %u, live bytes %u",
        region, region->GetRegionStart(), region->GetRegionAllocatedSize(), region->GetRegionEnd(),
        region->GetRegionType(), region->GetLiveByteCount());

    if (region->GetLiveByteCount() == 0) {
        CollectRegion(region);
        return;
    }

    if (!RouteRegion(region)) {
        return;
    }

    int32_t rawPointerCount = region->GetRawPointerObjectCount();
    CHECK(rawPointerCount == 0);
    Collector& collector = Heap::GetHeap().GetCollector();
    bool forwarded = region->VisitLiveObjectsUntilFalse(
        [&collector](BaseObject* obj) { return collector.ForwardObject(obj); });

    CHECK(forwarded);
    {
        region->SetRouteState(RegionInfo::RouteState::FORWARDED);
        CollectRegion(region);
    }
}

uintptr_t RegionManager::AllocPinnedFromFreeList(size_t size)
{
    std::lock_guard<std::mutex> lock(freePinnedSlotListMutex);
    GCPhase mutatorPhase = Mutator::GetMutator()->GetMutatorPhase();
    // For preventing missing mark, do not allocate object from slot list when gc phase is post trace.
    if (mutatorPhase == GCPhase::GC_PHASE_POST_TRACE) {
        return 0;
    }
    uintptr_t allocPtr = freePinnedSlotLists.PopFront(size);
    // For making bitmap comform with live object count, do not mark object repeated.
    if (allocPtr == 0 ||
        (mutatorPhase != GCPhase::GC_PHASE_ENUM &&
        mutatorPhase != GCPhase::GC_PHASE_TRACE &&
        mutatorPhase != GCPhase::GC_PHASE_CLEAR_SATB_BUFFER)) {
        return allocPtr;
    }
 
    // Mark new allocated pinned object.
    BaseObject* object = reinterpret_cast<BaseObject*>(allocPtr);
    (reinterpret_cast<CopyCollector*>(&Heap::GetHeap().GetCollector()))->MarkObject(object);
    return allocPtr;
}
} // namespace MapleRuntime
