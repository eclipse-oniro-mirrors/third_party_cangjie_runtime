// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#include "Concurrency.h"

#include <pthread.h>
#include <thread>

#include "Base/Log.h"
#include "CJThreadModel/CJThreadModel.h"
#include "schedule.h"

namespace MapleRuntime {
void Concurrency::Init(const ConcurrencyParam param, ScheduleType type)
{
    concurrencyModel = new (std::nothrow) CJThreadModel();
    CHECK_DETAIL(concurrencyModel != nullptr, "new Concurrency failed");
    concurrencyModel->Init(param, type);
    processorNum = param.processorNum;
}
} // namespace MapleRuntime
