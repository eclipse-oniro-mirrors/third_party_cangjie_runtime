/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * This source file is part of the Cangjie project, licensed under Apache-2.0
 * with Runtime Library Exception.
 *
 * See https://cangjie-lang.cn/pages/LICENSE for license information.
 */

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.

/**
 * @file
 *
 * This file declares CpuCounter related apis.
 */

#ifndef CANGJIE_STD_CPU_COUNTERS_H
#define CANGJIE_STD_CPU_COUNTERS_H

#include <stdint.h>

#ifdef __linux__
int InitPerf(int counter);
uint64_t ReadPerf(void);
#endif // ifdef __linux__

#if defined(__x86_64__) || defined(_M_X64) || defined(i386) \
    || defined(__i386__) || defined(__i386) || defined(_M_IX86)
uint64_t GetRdtscp(void);
uint64_t GetRdtsc(void);
#endif

#endif // CANGJIE_STD_CPU_COUNTERS_H
