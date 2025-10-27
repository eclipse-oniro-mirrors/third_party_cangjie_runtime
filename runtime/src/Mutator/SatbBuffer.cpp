// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#include "SatbBuffer.h"
#include "Heap/Allocator/RegionSpace.h"

#include "Base/ImmortalWrapper.h"

namespace MapleRuntime {
static ImmortalWrapper<SatbBuffer> g_instance;

SatbBuffer& SatbBuffer::Instance() noexcept { return *g_instance; }

bool SatbBuffer::ShouldEnqueue(const BaseObject* obj)
{
    if (UNLIKELY(obj == nullptr)) {
        return false;
    }
    return RegionSpace::ShouldEnqueue(obj);
}

static ImmortalWrapper<WeakRefBuffer> g_weakRefBuffer;

WeakRefBuffer& WeakRefBuffer::Instance() noexcept { return *g_weakRefBuffer; }
} // namespace MapleRuntime
