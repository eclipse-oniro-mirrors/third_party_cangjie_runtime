// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#ifndef MRT_NATIVE_ALLOCATOR_H
#define MRT_NATIVE_ALLOCATOR_H

#include <algorithm>
#include <mutex>
#include <thread>

#include "PagePool.h"
#include "ThreadCache.h"

namespace MapleRuntime {
class NativeAllocator {
public:
    static void* NativeAlloc(size_t bytes);
    static void NativeFree(void* ptr, size_t bytes);
};
} // namespace MapleRuntime
#endif
