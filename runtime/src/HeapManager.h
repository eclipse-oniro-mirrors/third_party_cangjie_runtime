// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#ifndef MRT_HEAP_MANAGER_H
#define MRT_HEAP_MANAGER_H

#include "Base/Macros.h"
#include "Common/TypeDef.h"
#include "Heap/Collector/GcRequest.h"
#include "RuntimeConfig.h"

namespace MapleRuntime {
// replace this for Heap.
class HeapManager {
public:
    HeapManager();
    ~HeapManager() = default;

    // runtime required lifecycle interfaces
    void Init(const HeapParam& param);
    void Fini();

    // alloc returns a memory address, not an "object" pointer that has not been initialized
    static MAddress Allocate(size_t allocSize, AllocType allocType = AllocType::MOVEABLE_OBJECT);
    static inline void RequestGC(GCReason reason, bool async);
};
} // namespace MapleRuntime
#endif // MRT_HEAP_MANAGER_H
