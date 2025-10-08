/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * This source file is part of the Cangjie project, licensed under Apache-2.0
 * with Runtime Library Exception.
 *
 * See https://cangjie-lang.cn/pages/LICENSE for license information.
 */

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.

#include <stdint.h>
#include <stdlib.h>
#if defined(_WIN32) && defined(__MINGW64__)

extern uint64_t CJ_Gettid(void)
{
    return (uint64_t)GetCurrentThreadId();
}

#elif defined(__APPLE__)
#include <pthread.h>

extern uint64_t CJ_Gettid(void)
{
    uint64_t nativeId;
    (void)pthread_threadid_np(NULL, &nativeId);
    return (uint64_t)nativeId;
}

#else
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <signal.h>

extern uint64_t CJ_Gettid(void)
{
    return (uint64_t)syscall(SYS_gettid);
}

#endif