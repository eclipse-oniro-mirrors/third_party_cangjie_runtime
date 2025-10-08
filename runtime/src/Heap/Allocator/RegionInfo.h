// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#ifndef MRT_REGION_INFO_H
#define MRT_REGION_INFO_H

#include <list>
#include <map>
#include <set>
#include <thread>
#include <vector>
#ifdef _WIN64
#include <memoryapi.h>
#include <errhandlingapi.h>
#include <handleapi.h>
#else
#include <sys/mman.h>
#endif
#include "Base/Globals.h"
#include "Base/MemUtils.h"
#include "Base/Panic.h"
#include "Base/RwLock.h"
#include "Heap/Collector/ForwardDataManager.h"
#include "Heap/Collector/GcInfos.h"
#include "Heap/Collector/LiveInfo.h"
#include "securec.h"
#ifdef CANGJIE_ASAN_SUPPORT
#include "Sanitizer/SanitizerInterface.h"
#endif

namespace MapleRuntime {
template<typename T>
class BitField {
public:
    // pos: the position where the bit locates. It starts from 0.
    // bitLen: the length that is to be read.
    T GetAtomicValue(size_t pos, size_t bitLen) const
    {
        T value = __atomic_load_n(&fieldVal, __ATOMIC_ACQUIRE);
        T bitMask = ((1 << bitLen) - 1) << pos;
        return value & bitMask;
    }
    void SetAtomicValue(size_t pos, size_t bitLen, T newValue)
    {
        do {
            T oldValue = fieldVal;
            T bitMask = ((1 << bitLen) - 1) << pos;
            T unchangedBitMask = ~bitMask;
            T newFieldValue = ((newValue << pos) & bitMask) | (oldValue & unchangedBitMask);
            if (__atomic_compare_exchange_n(&fieldVal, &oldValue, newFieldValue, false, __ATOMIC_ACQ_REL,
                                            __ATOMIC_ACQUIRE)) {
                return;
            }
        } while (true);
    }

private:
    T fieldVal;
};
// this class is the metadata of region, it contains all the information needed to manage its corresponding memory.
// Region memory is composed of several Units, described by UnitInfo.
// sizeof(RegionInfo) must be equal to sizeof(UnitInfo). We rely on this fact to calculate region-related address.

// region info is stored in the metadata of its primary unit (i.e. the first unit).
class RegionInfo {
public:
    enum RouteState : uint8_t {
        NORMAL = 0,
        FORWARDABLE,
        ROUTING,
        ROUTED,
        COMPACTED,
        FORWARDED,
    };

    static const size_t UNIT_SIZE; // same as system page size

    // regarding a object as a large object when the size is greater than 8 units.
    static const size_t LARGE_OBJECT_DEFAULT_THRESHOLD;

    // release a large object when the size is greater than 4096KB.
    static constexpr size_t LARGE_OBJECT_RELEASE_THRESHOLD = 4096 * KB;

    bool CompareExchangeRouteState(RouteState expected, RouteState newWord)
    {
#if defined(__x86_64__)
        bool success = __atomic_compare_exchange_n(&(metadata.routeState), &expected, newWord, true, __ATOMIC_ACQ_REL,
                                                   __ATOMIC_ACQUIRE);
#else
        // due to "Spurious Failure" of compare_exchange_weak, compare_exchange_strong is chosen.
        bool success = __atomic_compare_exchange_n(&(metadata.routeState), &expected, newWord, false, __ATOMIC_SEQ_CST,
                                                   __ATOMIC_ACQUIRE);
#endif
        return success;
    }

    RouteState GetRouteState() const
    {
        RouteState state = __atomic_load_n(&(metadata.routeState), std::memory_order_acquire);
        return state;
    }

    void SetRouteState(RouteState state) { __atomic_store_n(&(metadata.routeState), state, std::memory_order_release); }

    bool IsCompacted() { return GetRouteState() == RouteState::COMPACTED; }

    bool IsRoutingState() { return GetRouteState() == RouteState::ROUTING; }

    bool TryLockRouting(RouteState curState)
    {
        if (IsRoutingState()) {
            return false;
        }
        return CompareExchangeRouteState(curState, RouteState::ROUTING);
    }

    size_t GetPreLiveBytesInGhostRegion(MAddress address)
    {
        DCHECK(metadata.liveInfo0 != nullptr);
        size_t offset = GetAddressOffset(address);
        return metadata.liveInfo0->GetPreLiveBytes(offset, GetGhostRegionSize());
    }

    RegionInfo()
    {
        metadata.allocPtr = reinterpret_cast<uintptr_t>(nullptr);
        metadata.regionEnd = reinterpret_cast<uintptr_t>(nullptr);
    }
    static inline RegionInfo* NullRegion()
    {
        static RegionInfo nullRegion;
        return &nullRegion;
    }

    LiveInfo* GetLiveInfo()
    {
        LiveInfo* liveInfo = __atomic_load_n(&metadata.liveInfo, std::memory_order_acquire);
        if (reinterpret_cast<MAddress>(liveInfo) == LiveInfo::TEMPORARY_PTR) {
            return nullptr;
        }
        return liveInfo;
    }

    LiveInfo* GetOrAllocLiveInfo()
    {
        do {
            LiveInfo* liveInfo = __atomic_load_n(&metadata.liveInfo, std::memory_order_acquire);
            if (UNLIKELY(reinterpret_cast<uintptr_t>(liveInfo) == LiveInfo::TEMPORARY_PTR)) {
                continue;
            }
            if (LIKELY(liveInfo != nullptr)) {
                return liveInfo;
            }
            LiveInfo* newValue = reinterpret_cast<LiveInfo*>(LiveInfo::TEMPORARY_PTR);
            if (__atomic_compare_exchange_n(&metadata.liveInfo, &liveInfo, newValue, false, std::memory_order_seq_cst,
                                            std::memory_order_relaxed)) {
                LiveInfo* allocatedLiveInfo = ForwardDataManager::GetForwardDataManager().AllocateLiveInfo();
                allocatedLiveInfo->bindedRegion = this;
                __atomic_store_n(&metadata.liveInfo, allocatedLiveInfo, std::memory_order_release);
                DLOG(REGION, "region %p@%#zx liveinfo %p", this, GetRegionStart(), metadata.liveInfo);
                return allocatedLiveInfo;
            }
        } while (true);

        return nullptr;
    }

    RegionBitmap* GetMarkBitmap()
    {
        LiveInfo* liveInfo = GetLiveInfo();
        if (liveInfo == nullptr) {
            return nullptr;
        }
        RegionBitmap* bitmap = __atomic_load_n(&liveInfo->markBitmap, std::memory_order_acquire);
        if (reinterpret_cast<MAddress>(bitmap) == LiveInfo::TEMPORARY_PTR) {
            return nullptr;
        }
        return bitmap;
    }

    RegionBitmap* GetOrAllocMarkBitmap()
    {
        LiveInfo* liveInfo = GetOrAllocLiveInfo();
        do {
            RegionBitmap* bitmap = __atomic_load_n(&liveInfo->markBitmap, std::memory_order_acquire);
            if (UNLIKELY(reinterpret_cast<uintptr_t>(bitmap) == LiveInfo::TEMPORARY_PTR)) {
                continue;
            }
            if (LIKELY(bitmap != nullptr)) {
                return bitmap;
            }
            RegionBitmap* newValue = reinterpret_cast<RegionBitmap*>(LiveInfo::TEMPORARY_PTR);
            if (__atomic_compare_exchange_n(&liveInfo->markBitmap, &bitmap, newValue, false, std::memory_order_seq_cst,
                                            std::memory_order_relaxed)) {
                RegionBitmap* allocated =
                    ForwardDataManager::GetForwardDataManager().AllocateRegionBitmap(GetRegionSize());
                __atomic_store_n(&liveInfo->markBitmap, allocated, std::memory_order_release);
                DLOG(REGION, "region %p@%#zx markbitmap %p", this, GetRegionStart(), metadata.liveInfo->markBitmap);
                return allocated;
            }
        } while (true);

        return nullptr;
    }

    RegionBitmap* GetResurrectBitmap()
    {
        LiveInfo* liveInfo = GetLiveInfo();
        if (liveInfo == nullptr) {
            return nullptr;
        }
        RegionBitmap* bitmap = __atomic_load_n(&liveInfo->resurrectBitmap, std::memory_order_acquire);
        if (reinterpret_cast<MAddress>(bitmap) == LiveInfo::TEMPORARY_PTR) {
            return nullptr;
        }
        return bitmap;
    }

    RegionBitmap* GetOrAllocResurrectBitmap()
    {
        LiveInfo* liveInfo = GetOrAllocLiveInfo();
        do {
            RegionBitmap* bitmap = __atomic_load_n(&liveInfo->resurrectBitmap, std::memory_order_acquire);
            if (UNLIKELY(reinterpret_cast<uintptr_t>(bitmap) == LiveInfo::TEMPORARY_PTR)) {
                continue;
            }
            if (LIKELY(bitmap != nullptr)) {
                return bitmap;
            }
            RegionBitmap* newValue = reinterpret_cast<RegionBitmap*>(LiveInfo::TEMPORARY_PTR);
            if (__atomic_compare_exchange_n(&liveInfo->resurrectBitmap, &bitmap, newValue, false,
                                            std::memory_order_seq_cst, std::memory_order_relaxed)) {
                RegionBitmap* allocated =
                    ForwardDataManager::GetForwardDataManager().AllocateRegionBitmap(GetRegionSize());
                __atomic_store_n(&liveInfo->resurrectBitmap, allocated, std::memory_order_release);
                DLOG(REGION, "region %p@%#zx resurrectbitmap %p", this, GetRegionStart(),
                     metadata.liveInfo->resurrectBitmap);
                return allocated;
            }
        } while (true);

        return nullptr;
    }

    RegionBitmap* GetEnqueueBitmap()
    {
        LiveInfo* liveInfo = GetLiveInfo();
        if (liveInfo == nullptr) {
            return nullptr;
        }
        RegionBitmap* bitmap = __atomic_load_n(&liveInfo->enqueueBitmap, std::memory_order_acquire);
        if (reinterpret_cast<MAddress>(bitmap) == LiveInfo::TEMPORARY_PTR) {
            return nullptr;
        }
        return bitmap;
    }

    RegionBitmap* GetOrAllocEnqueueBitmap()
    {
        LiveInfo* liveInfo = GetOrAllocLiveInfo();
        do {
            RegionBitmap* bitmap = __atomic_load_n(&liveInfo->enqueueBitmap, std::memory_order_acquire);
            if (UNLIKELY(reinterpret_cast<uintptr_t>(bitmap) == LiveInfo::TEMPORARY_PTR)) {
                continue;
            }
            if (LIKELY(bitmap != nullptr)) {
                return bitmap;
            }
            RegionBitmap* newValue = reinterpret_cast<RegionBitmap*>(LiveInfo::TEMPORARY_PTR);
            if (__atomic_compare_exchange_n(&liveInfo->enqueueBitmap, &bitmap, newValue, false,
                                            std::memory_order_seq_cst, std::memory_order_relaxed)) {
                RegionBitmap* allocated =
                    ForwardDataManager::GetForwardDataManager().AllocateRegionBitmap(GetRegionSize());
                __atomic_store_n(&liveInfo->enqueueBitmap, allocated, std::memory_order_release);
                DLOG(REGION, "region %p@%#zx enqueuebitmap %p", this, GetRegionStart(),
                     metadata.liveInfo->enqueueBitmap);
                return allocated;
            }
        } while (true);

        return nullptr;
    }

    void ResetMarkBit()
    {
        SetMarkedRegionFlag(0);
        SetEnqueuedRegionFlag(0);
        SetResurrectedRegionFlag(0);
    }

    bool MarkObject(const BaseObject* obj)
    {
        if (IsLargeRegion()) {
            if (metadata.isMarked != 1) {
                SetMarkedRegionFlag(1);
                return false;
            }
            return true;
        }
        U32 size = obj->GetSize();
        size_t offset = GetAddressOffset(reinterpret_cast<MAddress>(obj));
        bool marked = GetOrAllocMarkBitmap()->MarkBits(offset, size, GetRegionSize());
        DCHECK(IsMarkedObject(obj));
        return marked;
    }

    bool ResurrentObject(const BaseObject* obj)
    {
        if (IsLargeRegion()) {
            if (metadata.isResurrected != 1) {
                SetResurrectedRegionFlag(1);
                return false;
            }
            return true;
        }
        U32 size = obj->GetSize();
        size_t offset = GetAddressOffset(reinterpret_cast<MAddress>(obj));
        bool marked = GetOrAllocResurrectBitmap()->MarkBits(offset, size, GetRegionSize());
        CHECK(IsResurrectedObject(obj));
        return marked;
    }

    bool EnqueueObject(const BaseObject* obj)
    {
        if (IsLargeRegion()) {
            if (metadata.isEnqueued != 1) {
                SetEnqueuedRegionFlag(1);
                return false;
            }
            return true;
        }
        U32 size = obj->GetSize();
        size_t offset = GetAddressOffset(reinterpret_cast<MAddress>(obj));
        bool marked = GetOrAllocEnqueueBitmap()->MarkBits(offset, size, GetRegionSize());
        CHECK(IsEnqueuedObject(obj));
        return marked;
    }

    bool IsResurrectedObject(const BaseObject* obj)
    {
        if (IsLargeRegion()) {
            return (metadata.isResurrected == 1);
        }
        RegionBitmap* resurrectBitmap = GetResurrectBitmap();
        if (resurrectBitmap == nullptr) {
            return false;
        }
        size_t offset = GetAddressOffset(reinterpret_cast<MAddress>(obj));
        return resurrectBitmap->IsMarked(offset);
    }

    bool IsMarkedObject(const BaseObject* obj)
    {
        if (IsLargeRegion()) {
            return (metadata.isMarked == 1);
        }
        RegionBitmap* markBitmap = GetMarkBitmap();
        if (markBitmap == nullptr) {
            return false;
        }
        size_t offset = GetAddressOffset(reinterpret_cast<MAddress>(obj));
        return markBitmap->IsMarked(offset);
    }

    bool IsEnqueuedObject(const BaseObject* obj)
    {
        if (IsLargeRegion()) {
            return (metadata.isEnqueued == 1);
        }
        RegionBitmap* enqueBitmap = GetEnqueueBitmap();
        if (enqueBitmap == nullptr) {
            return false;
        }
        size_t offset = GetAddressOffset(reinterpret_cast<MAddress>(obj));
        return enqueBitmap->IsMarked(offset);
    }

    ALWAYS_INLINE size_t GetAddressOffset(MAddress address)
    {
        DCHECK(GetRegionStart() <= address);
        return (address - GetRegionStart());
    }

    enum class UnitRole : uint8_t {
        // for the head unit
        FREE_UNITS = 0,
        SMALL_SIZED_UNITS,
        LARGE_SIZED_UNITS,

        SUBORDINATE_UNIT,
    };

    // region is and must be one of following types during its whole lifecycle.
    // one-to-one mapping to region-lists.

    enum class RegionType : uint8_t {
        FREE_REGION,

        THREAD_LOCAL_REGION,
        RECENT_FULL_REGION,
        FROM_REGION,
        LONE_FROM_REGION,
        UNMOVABLE_FROM_REGION,
        TO_REGION,

        // pinned object will not be forwarded by concurrent copying gc.
        FULL_PINNED_REGION,
        RECENT_PINNED_REGION,

        // region for raw-pointer objects which are exposed to runtime thus can not be moved by any gc.
        // raw-pointer region becomes pinned region when none of its member objects are used as raw pointer.
        RAW_POINTER_PINNED_REGION,

        // allocation context is able and responsible to determine whether it is safe to be collected.
        // There are two kind of region, and the type depends on the allocation size.
        TL_RAW_POINTER_REGION,
        TL_LARGE_RAW_POINTER_REGION,

        LARGE_REGION,
        RECENT_LARGE_REGION,

        GARBAGE_REGION,
    };

    static void Initialize(size_t nUnit, uintptr_t regionInfoAddr, uintptr_t heapAddress)
    {
        UnitInfo::totalUnitCount = nUnit;
        UnitInfo::unitInfoStart = regionInfoAddr;
        UnitInfo::heapStartAddress = heapAddress;
    }

    static RegionInfo* GetRegionInfo(uint32_t idx)
    {
        UnitInfo* unit = RegionInfo::UnitInfo::GetUnitInfo(idx);
        if (static_cast<UnitRole>(unit->GetMetadata().unitRole) == UnitRole::SUBORDINATE_UNIT) {
            return unit->GetMetadata().ownerRegion;
        }
        return reinterpret_cast<RegionInfo*>(unit);
    }

    static RegionInfo* GetRegionInfoAt(uintptr_t allocAddr)
    {
        UnitInfo* unit = RegionInfo::UnitInfo::GetUnitInfoAt(allocAddr);
        if (static_cast<UnitRole>(unit->GetMetadata().unitRole) == UnitRole::SUBORDINATE_UNIT) {
            return unit->GetMetadata().ownerRegion;
        }
        return reinterpret_cast<RegionInfo*>(unit);
    }

    static bool InGhostFromRegion(BaseObject* obj)
    {
        UnitInfo* unit = RegionInfo::UnitInfo::GetUnitInfoAt(reinterpret_cast<uintptr_t>(obj));
        return unit->GetMetadata().inGhostFromRegion != 0;
    }

    static RegionInfo* GetGhostFromRegionAt(uintptr_t allocAddr)
    {
        UnitInfo* unit = RegionInfo::UnitInfo::GetUnitInfoAt(allocAddr);
        if (unit->GetMetadata().inGhostFromRegion == 0) {
            return nullptr;
        }
        if (static_cast<UnitRole>(unit->GetMetadata().unitRole0) == UnitRole::SUBORDINATE_UNIT) {
            return unit->GetMetadata().ownerRegion0;
        }
        return reinterpret_cast<RegionInfo*>(unit);
    }

    static void InitFreeRegion(size_t unitIdx, size_t nUnit)
    {
        RegionInfo* region = reinterpret_cast<RegionInfo*>(RegionInfo::UnitInfo::GetUnitInfo(unitIdx));
        region->InitRegionInfo(nUnit, UnitRole::FREE_UNITS);
    }

    static RegionInfo* InitRegion(size_t unitIdx, size_t nUnit, RegionInfo::UnitRole uclass)
    {
        RegionInfo* region = reinterpret_cast<RegionInfo*>(RegionInfo::UnitInfo::GetUnitInfo(unitIdx));
        region->InitRegion(nUnit, uclass);
        return region;
    }

    static RegionInfo* InitRegionAt(uintptr_t addr, size_t nUnit, RegionInfo::UnitRole uclass)
    {
        size_t idx = RegionInfo::UnitInfo::GetUnitIdxAt(addr);
        return InitRegion(idx, nUnit, uclass);
    }

    static MAddress GetUnitAddress(size_t unitIdx) { return UnitInfo::GetUnitAddress(unitIdx); }

    static void ClearUnits(size_t idx, size_t cnt)
    {
        uintptr_t unitAddress = RegionInfo::GetUnitAddress(idx);
        size_t size = cnt * RegionInfo::UNIT_SIZE;
        DLOG(REGION, "clear dirty units[%zu+%zu, %zu) @[%#zx+%zu, %#zx)", idx, cnt, idx + cnt, unitAddress, size,
             RegionInfo::GetUnitAddress(idx + cnt));
        MapleRuntime::MemorySet(unitAddress, size, 0, size);
    }

    static void ReleaseUnits(size_t idx, size_t cnt)
    {
        void* unitAddress = reinterpret_cast<void*>(RegionInfo::GetUnitAddress(idx));
        size_t size = cnt * RegionInfo::UNIT_SIZE;
        DLOG(REGION, "release physical memory for units [%zu+%zu, %zu) @[%p+%zu, 0x%zx)", idx, cnt, idx + cnt,
             unitAddress, size, RegionInfo::GetUnitAddress(idx + cnt));
#if defined(_WIN64)
        CHECK_E(UNLIKELY(!VirtualFree(unitAddress, size, MEM_DECOMMIT)),
                "VirtualFree failed in ReturnPage, errno: %s", GetLastError());
        
#elif defined(__APPLE__)
        MemorySet(reinterpret_cast<uintptr_t>(unitAddress), size, 0, size);
        (void)madvise(unitAddress, size, MADV_DONTNEED);
#else
        (void)madvise(unitAddress, size, MADV_DONTNEED);
#endif
#ifdef CANGJIE_ASAN_SUPPORT
        Sanitizer::OnHeapMadvise(unitAddress, size);
#endif
    }

    BaseObject* GetFirstObject() const { return reinterpret_cast<BaseObject*>(GetRegionStart()); }

    bool IsEmpty() const
    {
        MRT_ASSERT(IsSmallRegion(), "wrong region type");
        return GetRegionAllocPtr() == GetRegionStart();
    }

    size_t GetRegionSize() const
    {
        DCHECK(metadata.regionEnd > GetRegionStart());
        return metadata.regionEnd - GetRegionStart();
    }

    size_t GetUnitCount() const { return GetRegionSize() / UNIT_SIZE; }

    size_t GetGhostRegionSize() const
    {
        DCHECK(metadata.regionEnd0 > GetRegionStart());
        return metadata.regionEnd0 - GetRegionStart();
    }

    size_t GetGhostRegionUnitCount() const { return GetGhostRegionSize() / UNIT_SIZE; }

    size_t GetAvailableSize() const
    {
        MRT_ASSERT(IsSmallRegion(), "wrong region type");
        return GetRegionEnd() - GetRegionAllocPtr();
    }

    size_t GetRegionAllocatedSize() const { return GetRegionAllocPtr() - GetRegionStart(); }

#if defined(GCINFO_DEBUG) && GCINFO_DEBUG
    void DumpRegionInfo(LogType type) const;
    const char* GetTypeName() const;
#endif

    void VisitAllObjects(const std::function<void(BaseObject*)>&& func);
    bool VisitLiveObjectsUntilFalse(const std::function<bool(BaseObject*)>&& func);

    // reset so that this region can be reused for allocation
    void InitFreeUnits()
    {
        size_t nUnit = GetUnitCount();
        UnitInfo* unit = reinterpret_cast<UnitInfo*>(this);
        for (size_t i = 0; i < nUnit; ++i) {
            unit[i].ToFreeRegion();
        }
    }

    void SetRouteInfo(uintptr_t to1, uint32_t to1used = 0, uint32_t to2 = RouteInfo::INVALID_VALUE)
    {
        metadata.routeInfo.SetRouteInfo(to1, to1used, to2);
    }

    BaseObject* GetRoute(BaseObject* fromObj)
    {
        MAddress fromAddress = reinterpret_cast<MAddress>(fromObj);
        uint64_t preLiveBytes = GetPreLiveBytesInGhostRegion(fromAddress);
        MAddress toAddr = metadata.routeInfo.GetRoute(preLiveBytes);
        return reinterpret_cast<BaseObject*>(toAddr);
    }

    void PrepareForwardableRegion()
    {
        CHECK(IsFromRegion());
        CHECK(static_cast<UnitRole>(metadata.unitRole) == UnitRole::SMALL_SIZED_UNITS);
        CHECK(metadata.inGhostFromRegion == 0);
        metadata.routeState = FORWARDABLE;
        SetUnitRole0(static_cast<UnitRole>(metadata.unitRole));
        metadata.liveInfo0 = metadata.liveInfo;
        metadata.regionEnd0 = metadata.regionEnd;
        metadata.routeInfo.SetRouteInfo(0);
        if (GetLiveByteCount() > 0) {
            SetInGhostRegion(1);
        }

        metadata.nextRegionIdx0 = metadata.nextRegionIdx;

        // prepare all units of this region.
        UnitInfo* unit = reinterpret_cast<UnitInfo*>(this);
        size_t nUnit = GetUnitCount();
        for (size_t i = 1; i < nUnit; i++) {
            UnitMetadata& mdata = unit[i].GetMetadata();
            CHECK(static_cast<UnitRole>(mdata.unitRole) == UnitRole::SUBORDINATE_UNIT);
            CHECK(mdata.ownerRegion == this);
            CHECK(mdata.inGhostFromRegion == 0);

            unit[i].SetUnitRole0(UnitRole::SUBORDINATE_UNIT);
            mdata.ownerRegion0 = this;
            if (GetLiveByteCount() > 0) {
                unit[i].SetInGhostRegion(1);
            }
        }
    }

    void ClearGhostRegionBit()
    {
        if (IsGhostFromRegion()) {
            UnitInfo* unit = reinterpret_cast<UnitInfo*>(this);
            size_t nUnit = GetUnitCount();
            for (size_t i = 0; i < nUnit; i++) {
                unit[i].SetInGhostRegion(0);
            }
        }
    }
    // dispel all units of this region.
    // inGhostFromRegion is the unique guard condition.
    void DispelGhostFromRegion()
    {
        metadata.routeState = NORMAL;
        UnitInfo* unit = reinterpret_cast<UnitInfo*>(this);
        size_t nUnit = GetGhostRegionUnitCount();
        for (size_t i = 0; i < nUnit; i++) {
            unit[i].SetInGhostRegion(0);
        }
    }

    bool IsGhostFromRegion() const { return metadata.inGhostFromRegion == 1; }

    // the interface can only be used to clear live info after gc.
    void CheckAndClearLiveInfo(LiveInfo* liveInfo)
    {
        // Garbage region may be reused by other thread. For the sake of safety, we don't clean it here.
        // We will clean it before the region is accessable.
        if (IsGarbageRegion()) {
            return;
        }
        // Check the value whether is expected, in order to avoid resetting a reused region.
        if (metadata.liveInfo == liveInfo) {
            metadata.liveInfo = nullptr;
            metadata.liveByteCount = 0;
        }
    }
    void ClearLiveInfo()
    {
        if (metadata.liveInfo != nullptr) {
            metadata.liveInfo = nullptr;
        }
        metadata.liveByteCount = 0;
    }

    // only from-region should be locked.
    bool TryLockReadFromRegion()
    {
        if (metadata.rwLock.TryLockRead()) {
            if (IsFromRegion() || IsLoneFromRegion()) {
                return true;
            } else {
                metadata.rwLock.UnlockRead();
            }
        }
        return false;
    }

    void UnlockReadFromRegion() { metadata.rwLock.UnlockRead(); }

    void LockWriteRegion() { metadata.rwLock.LockWrite(); }

    void UnlockWriteRegion() { metadata.rwLock.UnlockWrite(); }

    // These interfaces are used to make sure the writing operations of value in C++ Bit Field will be atomic.
    void SetUnitRole(UnitRole role)
    {
        metadata.unitRoleBitField.SetAtomicValue(0, BIT_LENGTH, static_cast<uint8_t>(role));
    }
    void SetUnitRole0(UnitRole role)
    {
        metadata.unitRoleBitField.SetAtomicValue(BIT_LENGTH, BIT_LENGTH, static_cast<uint8_t>(role));
    }
    void SetRegionType(RegionType type)
    {
        metadata.regionStateBitField.SetAtomicValue(RegionStateBitPos::REGION_TYPE_FLAG, BIT_LENGTH,
                                                    static_cast<uint8_t>(type));
    }
    void SetTraceRegionFlag(uint8_t flag)
    {
        metadata.regionStateBitField.SetAtomicValue(RegionStateBitPos::TRACE_REGION_FLAG, 1, flag);
    }
    void SetInGhostRegion(uint8_t flag)
    {
        metadata.regionStateBitField.SetAtomicValue(RegionStateBitPos::IN_GHOST_FROM_REGION_FLAG, 1, flag);
    }

    void SetMarkedRegionFlag(uint8_t flag)
    {
        metadata.regionStateBitField.SetAtomicValue(RegionStateBitPos::MARKED_REGION_FLAG, 1, flag);
    }
    void SetEnqueuedRegionFlag(uint8_t flag)
    {
        metadata.regionStateBitField.SetAtomicValue(RegionStateBitPos::ENQUEUED_REGION_FLAG, 1, flag);
    }
    void SetResurrectedRegionFlag(uint8_t flag)
    {
        metadata.regionStateBitField.SetAtomicValue(RegionStateBitPos::RESURRECTED_REGION_FLAG, 1, flag);
    }

    RegionType GetRegionType() const { return static_cast<RegionType>(metadata.regionType); }
    UnitRole GetUnitRole() const { return static_cast<UnitRole>(metadata.unitRole); }

    size_t GetUnitIdx() const { return RegionInfo::UnitInfo::GetUnitIdx(reinterpret_cast<const UnitInfo*>(this)); }

    MAddress GetRegionStart() const { return RegionInfo::GetUnitAddress(GetUnitIdx()); }

    MAddress GetRegionEnd() const { return metadata.regionEnd; }

    void SetRegionAllocPtr(MAddress addr) { metadata.allocPtr = addr; }

    MAddress GetRegionAllocPtr() const { return metadata.allocPtr; }

    int32_t IncRawPointerObjectCount()
    {
        int32_t oldCount = __atomic_fetch_add(&metadata.rawPointerObjectCount, 1, __ATOMIC_SEQ_CST);
        CHECK_DETAIL(oldCount >= 0, "region %p has wrong raw pointer count %d", this);
        CHECK_DETAIL(oldCount < MAX_RAW_POINTER_COUNT, "inc raw-pointer-count overflow");
        return oldCount;
    }

    int32_t DecRawPointerObjectCount()
    {
        int32_t oldCount = __atomic_fetch_sub(&metadata.rawPointerObjectCount, 1, __ATOMIC_SEQ_CST);
        CHECK_DETAIL(oldCount > 0, "dec raw-pointer-count underflow, please check whether releaseRawData is overused.");
        return oldCount;
    }

    int32_t GetRawPointerObjectCount() const
    {
        return __atomic_load_n(&metadata.rawPointerObjectCount, __ATOMIC_SEQ_CST);
    }

    bool CompareAndSwapRawPointerObjectCount(int32_t expectVal, int32_t newVal)
    {
        return __atomic_compare_exchange_n(&metadata.rawPointerObjectCount, &expectVal, newVal, false, __ATOMIC_SEQ_CST,
                                           __ATOMIC_ACQUIRE);
    }

    uintptr_t Alloc(size_t size)
    {
        size_t limit = GetRegionEnd();
        if (metadata.allocPtr + size <= limit) {
            uintptr_t addr = metadata.allocPtr;
            metadata.allocPtr += size;
            return addr;
        } else {
            return 0;
        }
    }

    // for regions shared by multithreads
    uintptr_t AtomicAlloc(size_t size)
    {
        uintptr_t addr = __atomic_fetch_add(&metadata.allocPtr, size, __ATOMIC_ACQ_REL);
        // should not check allocPtr, because it might be shared
        if ((addr < GetRegionEnd()) && (size <= GetRegionEnd() - addr)) {
            return addr;
        }
        if (addr <= GetRegionEnd()) {
            __atomic_store_n(&metadata.allocPtr, addr, __ATOMIC_SEQ_CST);
        }
        return 0;
    }

    bool IsTraceRegion() const { return metadata.isTraceRegion == 1; }

    // copyable during concurrent copying gc.
    bool IsSmallRegion() const { return static_cast<UnitRole>(metadata.unitRole) == UnitRole::SMALL_SIZED_UNITS; }

    bool IsLargeRegion() const { return static_cast<UnitRole>(metadata.unitRole) == UnitRole::LARGE_SIZED_UNITS; }

    bool IsThreadLocalRegion() const
    {
        return static_cast<RegionType>(metadata.regionType) == RegionType::THREAD_LOCAL_REGION;
    }

    bool IsPinnedRegion() const
    {
        return (static_cast<RegionType>(metadata.regionType) == RegionType::FULL_PINNED_REGION) ||
            (static_cast<RegionType>(metadata.regionType) == RegionType::RECENT_PINNED_REGION);
    }

    RegionInfo* GetPrevRegion() const
    {
        if (UNLIKELY(metadata.prevRegionIdx == NULLPTR_IDX)) {
            return nullptr;
        }
        return reinterpret_cast<RegionInfo*>(UnitInfo::GetUnitInfo(metadata.prevRegionIdx));
    }

    void SetPrevRegion(const RegionInfo* r)
    {
        if (UNLIKELY(r == nullptr)) {
            metadata.prevRegionIdx = NULLPTR_IDX;
            return;
        }
        size_t prevIdx = r->GetUnitIdx();
        MRT_ASSERT(prevIdx < NULLPTR_IDX, "exceeds the maxinum limit for region info");
        metadata.prevRegionIdx = static_cast<uint32_t>(prevIdx);
    }

    RegionInfo* GetNextRegion() const
    {
        if (UNLIKELY(metadata.nextRegionIdx == NULLPTR_IDX)) {
            return nullptr;
        }
        DCHECK(metadata.nextRegionIdx < UnitInfo::totalUnitCount);
        return reinterpret_cast<RegionInfo*>(UnitInfo::GetUnitInfo(metadata.nextRegionIdx));
    }

    RegionInfo* GetNextGhostRegion() const
    {
        if (UNLIKELY(metadata.nextRegionIdx0 == NULLPTR_IDX)) {
            return nullptr;
        }
        DCHECK(metadata.nextRegionIdx0 < UnitInfo::totalUnitCount);
        return reinterpret_cast<RegionInfo*>(UnitInfo::GetUnitInfo(metadata.nextRegionIdx0));
    }

    void SetNextRegion(const RegionInfo* r)
    {
        if (UNLIKELY(r == nullptr)) {
            metadata.nextRegionIdx = NULLPTR_IDX;
            return;
        }
        size_t nextIdx = r->GetUnitIdx();
        MRT_ASSERT(nextIdx < NULLPTR_IDX, "exceeds the maxinum limit for region info");
        metadata.nextRegionIdx = static_cast<uint32_t>(nextIdx);
    }

    bool IsFromRegion() const { return static_cast<RegionType>(metadata.regionType) == RegionType::FROM_REGION; }
    bool IsLoneFromRegion() const
    {
        return static_cast<RegionType>(metadata.regionType) == RegionType::LONE_FROM_REGION;
    }
    bool IsUnmovableFromRegion() const
    {
        return static_cast<RegionType>(metadata.regionType) == RegionType::UNMOVABLE_FROM_REGION ||
            static_cast<RegionType>(metadata.regionType) == RegionType::RAW_POINTER_PINNED_REGION;
    }

    bool IsToRegion() const { return static_cast<RegionType>(metadata.regionType) == RegionType::TO_REGION; }

    bool IsGarbageRegion() const { return static_cast<RegionType>(metadata.regionType) == RegionType::GARBAGE_REGION; }
    bool IsFreeRegion() const { return static_cast<UnitRole>(metadata.unitRole) == UnitRole::FREE_UNITS; }

    bool IsValidRegion() const
    {
        return static_cast<UnitRole>(metadata.unitRole) == UnitRole::SMALL_SIZED_UNITS ||
            static_cast<UnitRole>(metadata.unitRole) == UnitRole::LARGE_SIZED_UNITS;
    }

    uint32_t GetLiveByteCount() const { return metadata.liveByteCount; }

    void ResetLiveByteCount() { metadata.liveByteCount = 0; }

    void AddLiveByteCount(uint32_t count)
    {
        (void)__atomic_fetch_add(&metadata.liveByteCount, count, __ATOMIC_ACQ_REL);
    }

    void RemoveFromList()
    {
        RegionInfo* prev = GetPrevRegion();
        RegionInfo* next = GetNextRegion();
        if (prev != nullptr) {
            prev->SetNextRegion(next);
        }
        if (next != nullptr) {
            next->SetPrevRegion(prev);
        }
        this->SetNextRegion(nullptr);
        this->SetPrevRegion(nullptr);
    }

private:
    static constexpr int32_t MAX_RAW_POINTER_COUNT = std::numeric_limits<int32_t>::max();
    static constexpr int32_t BIT_LENGTH = 4;
    enum RegionStateBitPos : uint8_t {
        REGION_TYPE_FLAG = 0,
        TRACE_REGION_FLAG = BIT_LENGTH,
        IN_GHOST_FROM_REGION_FLAG,
        MARKED_REGION_FLAG,
        ENQUEUED_REGION_FLAG,
        RESURRECTED_REGION_FLAG
    };

    struct UnitMetadata {
        struct { // basic data for RegionInfo
            // for fast allocation, always at the start.
            uintptr_t allocPtr;
            uintptr_t regionEnd;

            uint32_t nextRegionIdx;
            uint32_t prevRegionIdx; // support fast deletion for region list.

            uint32_t liveByteCount;
            int32_t rawPointerObjectCount;
        };

        union {
            LiveInfo* liveInfo = nullptr;
            RegionInfo* ownerRegion; // if unit is SUBORDINATE_UNIT
        };

        union {
            LiveInfo* liveInfo0 = nullptr;
            RegionInfo* ownerRegion0; // if unit is SUBORDINATE_UNIT
        };

        uintptr_t regionEnd0;
        RouteInfo routeInfo;

        // used to traverse ghost region.
        uint32_t nextRegionIdx0;

        // the writing operation in C++ Bit-Field feature is not atomic, if we wants to
        // change the value, we must use specific interface implenmented by BitField.
        union {
            struct {
                uint8_t unitRole : BIT_LENGTH;
                uint8_t unitRole0 : BIT_LENGTH; // unit class before forwarded and reclaimed.
            };
            BitField<uint8_t> unitRoleBitField;
        };

        // the writing operation in C++ Bit-Field feature is not atomic, if we wants to
        // change the value, we must use specific interface implenmented by BitField.
        union {
            struct {
                uint8_t regionType : BIT_LENGTH;

                // a region allocated during trace phase, gc should not put any object in this region into satb buffer.
                // the count of objects which can be put into satb buffer should has an upper-bound,
                // so that concurrent tracing can converge and terminate.
                uint8_t isTraceRegion : 1;

                // true if this unit belongs to a ghost region, which is an unreal region for keeping reclaimed
                // from-region. ghost region is set up to memorize a from-region before from-space is forwarded. this
                // flag is cleared when ghost-from-space is cleared. Note this flag is essentially important for
                // FindToVersion().
                uint8_t inGhostFromRegion : 1;
                uint8_t isMarked : 1;
                uint8_t isEnqueued : 1;
                uint8_t isResurrected : 1;
            };
            BitField<uint16_t> regionStateBitField;
        };
        RouteState routeState; // todo: put in RouteInfo
        RwLock rwLock;
    };

    class UnitInfo {
    public:
        // propgated from RegionManager
        static uintptr_t heapStartAddress; // the address of the first region space to allocate objects
        static size_t totalUnitCount;
        static uintptr_t unitInfoStart; // the address of the first UnitInfo

        constexpr static uint32_t INVALID_IDX = std::numeric_limits<uint32_t>::max();
        static size_t GetUnitIdxAt(uintptr_t allocAddr)
        {
            if (heapStartAddress <= allocAddr && allocAddr < (heapStartAddress + totalUnitCount * UNIT_SIZE)) {
                return (allocAddr - heapStartAddress) / UNIT_SIZE;
            }

            std::abort();
        }

        static UnitInfo* GetUnitInfoAt(uintptr_t allocAddr) { return GetUnitInfo(GetUnitIdxAt(allocAddr)); }

        // get the unit address by index
        static MAddress GetUnitAddress(size_t idx)
        {
            CHECK(idx < totalUnitCount);
            return heapStartAddress + idx * UNIT_SIZE;
        }

        static UnitInfo* GetUnitInfo(size_t idx)
        {
            CHECK(idx < totalUnitCount);
            return reinterpret_cast<UnitInfo*>(unitInfoStart + idx * sizeof(UnitInfo));
        }

        static size_t GetUnitIdx(const UnitInfo* unit)
        {
            uintptr_t ptr = reinterpret_cast<uintptr_t>(unit);
            if (unitInfoStart <= ptr && ptr < heapStartAddress) {
                return (ptr - unitInfoStart) / sizeof(UnitInfo);
            }

            LOG(RTLOG_FATAL, "UnitInfo::GetUnitIdx() Should not execute here, abort.");
            return 0;
        }

        UnitInfo() = delete;
        UnitInfo(const UnitInfo&) = delete;
        UnitInfo& operator=(const UnitInfo&) = delete;
        ~UnitInfo() = delete;

        // These interfaces are used to make sure the writing operations of value in C++ Bit Field will be atomic.
        void SetUnitRole(UnitRole role)
        {
            metadata.unitRoleBitField.SetAtomicValue(0, BIT_LENGTH, static_cast<uint8_t>(role));
        }
        void SetUnitRole0(UnitRole role)
        {
            metadata.unitRoleBitField.SetAtomicValue(BIT_LENGTH, BIT_LENGTH, static_cast<uint8_t>(role));
        }
        void SetRegionType(RegionType type)
        {
            metadata.regionStateBitField.SetAtomicValue(RegionStateBitPos::REGION_TYPE_FLAG, BIT_LENGTH,
                                                        static_cast<uint8_t>(type));
        }
        void SetTraceRegionFlag(uint8_t flag)
        {
            metadata.regionStateBitField.SetAtomicValue(RegionStateBitPos::TRACE_REGION_FLAG, 1, flag);
        }
        void SetInGhostRegion(uint8_t flag)
        {
            metadata.regionStateBitField.SetAtomicValue(RegionStateBitPos::IN_GHOST_FROM_REGION_FLAG, 1, flag);
        }

        void SetMarkedRegionFlag(uint8_t flag)
        {
            metadata.regionStateBitField.SetAtomicValue(RegionStateBitPos::MARKED_REGION_FLAG, 1, flag);
        }

        void SetEnqueuedRegionFlag(uint8_t flag)
        {
            metadata.regionStateBitField.SetAtomicValue(RegionStateBitPos::ENQUEUED_REGION_FLAG, 1, flag);
        }

        void SetResurrectedRegionFlag(uint8_t flag)
        {
            metadata.regionStateBitField.SetAtomicValue(RegionStateBitPos::RESURRECTED_REGION_FLAG, 1, flag);
        }

        void InitSubordinateUnit(RegionInfo* owner)
        {
            SetUnitRole(UnitRole::SUBORDINATE_UNIT);
            metadata.ownerRegion = owner;
        }

        void ToFreeRegion() { InitFreeRegion(GetUnitIdx(this), 1); }

        void ClearUnit() { ClearUnits(GetUnitIdx(this), 1); }

        void ReleaseUnit() { ReleaseUnits(GetUnitIdx(this), 1); }

        UnitMetadata& GetMetadata() { return metadata; }

        UnitRole GetUnitRole() const { return static_cast<UnitRole>(metadata.unitRole); }

    private:
        UnitMetadata metadata;
    };

    void InitRegionInfo(size_t nUnit, UnitRole uClass)
    {
        metadata.allocPtr = GetRegionStart();
        metadata.regionEnd = metadata.allocPtr + nUnit * RegionInfo::UNIT_SIZE;
        metadata.prevRegionIdx = NULLPTR_IDX;
        metadata.nextRegionIdx = NULLPTR_IDX;
        metadata.liveByteCount = 0;
        metadata.liveInfo = nullptr;
        SetRegionType(RegionType::FREE_REGION);
        SetUnitRole(uClass);
        SetTraceRegionFlag(0);
        SetMarkedRegionFlag(0);
        SetEnqueuedRegionFlag(0);
        SetResurrectedRegionFlag(0);
        __atomic_store_n(&metadata.rawPointerObjectCount, 0, __ATOMIC_SEQ_CST);
    }

    void InitRegion(size_t nUnit, UnitRole uClass)
    {
        InitRegionInfo(nUnit, uClass);

        // initialize region's subordinate units.
        UnitInfo* unit = reinterpret_cast<UnitInfo*>(this);
        for (size_t i = 1; i < nUnit; i++) {
            unit[i].InitSubordinateUnit(this);
        }
    }

    static constexpr uint32_t NULLPTR_IDX = UnitInfo::INVALID_IDX;
    UnitMetadata metadata;
};
} // namespace MapleRuntime
#endif // MRT_REGION_INFO_H
