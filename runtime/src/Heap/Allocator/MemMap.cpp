// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#include "MemMap.h"

#include <algorithm>
#ifdef _WIN64
#include <errhandlingapi.h>
#include <handleapi.h>
#include <memoryapi.h>
#endif

#include "Base/Log.h"
#include "Base/LogFile.h"
#include "Base/Panic.h"
#include "Base/SysCall.h"

namespace MapleRuntime {
using namespace std;

// not thread safe, do not call from multiple threads
MemMap* MemMap::MapMemory(size_t reqSize, size_t initSize, const Option& opt)
{
    void* mappedAddr = nullptr;
    reqSize = AllocUtilRndUp<size_t>(reqSize, ALLOC_UTIL_PAGE_SIZE);

#ifdef _WIN64
    // Windows don`t support to map a non-access memory, and virtualProtect() only can change page protections on
    // memory blocks allocated by GlobalAlloc(), HeapAlloc(), or LocalAlloc(). For mapped views, this value must be
    // compatible with the access protection specified when the view was mapped, which means we can`t crete a non-access
    // map view, than change the protections like linux procedure. Here we just map a PAGE_EXECUTE_READWRITE at
    // beginning.
    mappedAddr = VirtualAlloc(NULL, reqSize, MEM_RESERVE, PAGE_READWRITE);
#else
    DLOG(ALLOC, "MemMap::MapMemory size %zu", reqSize);
    mappedAddr = mmap(opt.reqBase, reqSize, PROT_NONE, opt.flags, -1, 0);
#endif

    bool failure = false;
#if defined(_WIN64) || defined(__APPLE__)
    if (mappedAddr != NULL) {
#else
    if (mappedAddr != MAP_FAILED) {
        (void)madvise(mappedAddr, reqSize, MADV_NOHUGEPAGE);
        MRT_PRCTL(mappedAddr, reqSize, opt.tag);
#endif
        // if protAll, all memory is protected at creation, and we never change it (save time)
        size_t protSize = opt.protAll ? reqSize : initSize;
        if (!ProtectMemInternal(mappedAddr, protSize, opt.prot)) {
            failure = true;
            LOG(RTLOG_ERROR, "MemMap::MapMemory mprotect failed");
            ALLOCUTIL_MEM_UNMAP(mappedAddr, reqSize);
        }
    } else {
        failure = true;
    }
    CHECK_DETAIL(!failure, "MemMap::MapMemory failed reqSize: %zu initSize: %zu", reqSize, initSize);

    DLOG(ALLOC, "MemMap::MapMemory size %zu successful at %p", reqSize, mappedAddr);
    MemMap* memMap = new (std::nothrow) MemMap(mappedAddr, initSize, reqSize);
    CHECK_DETAIL(memMap != nullptr, "new MemMap failed");
    return memMap;
}

#ifdef _WIN64
void MemMap::CommitMemory(void* addr, size_t size)
{
    CHECK_E(UNLIKELY(!VirtualAlloc(addr, size, MEM_COMMIT, PAGE_READWRITE)),
        "VirtualAlloc commit failed in GetPage, errno: %d", GetLastError());
}
#endif

MemMap::MemMap(void* baseAddr, size_t initSize, size_t mappedSize)
    : memBaseAddr(baseAddr), memCurrSize(initSize), memMappedSize(mappedSize)
{
    memCurrEndAddr = reinterpret_cast<void*>(reinterpret_cast<MAddress>(memBaseAddr) + memCurrSize);
    memMappedEndAddr = reinterpret_cast<void*>(reinterpret_cast<MAddress>(memBaseAddr) + memMappedSize);
}

bool MemMap::ProtectMemInternal(void* addr, size_t size, int prot)
{
    DLOG(ALLOC, "MemMap::ProtectMem %p, size %zu, prot %d", addr, size, prot);
#ifdef _WIN64
    return true;
#else
    int ret = mprotect(addr, size, prot);
    return (ret == 0);
#endif
}
MemMap::~MemMap()
{
    ALLOCUTIL_MEM_UNMAP(memBaseAddr, memMappedSize);
    memBaseAddr = nullptr;
    memCurrEndAddr = nullptr;
    memMappedEndAddr = nullptr;
}
} // namespace MapleRuntime
