/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * This source file is part of the Cangjie project, licensed under Apache-2.0
 * with Runtime Library Exception.
 *
 * See https://cangjie-lang.cn/pages/LICENSE for license information.
 */

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.

#include <stdint.h>
#include <time.h>

#define NANOSECOND 1000000000

extern int64_t CJ_RANDOM_NanoNow(void)
{
    struct timespec nano = {0, 0};
    (void)clock_gettime(CLOCK_REALTIME, &nano);
    return (int64_t)(nano.tv_sec) * (int64_t)NANOSECOND + (int64_t)(nano.tv_nsec);
}
