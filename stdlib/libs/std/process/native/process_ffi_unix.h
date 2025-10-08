/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * This source file is part of the Cangjie project, licensed under Apache-2.0
 * with Runtime Library Exception.
 *
 * See https://cangjie-lang.cn/pages/LICENSE for license information.
 */

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.

#ifndef PROCESS_FFI_UNIX_H
#define PROCESS_FFI_UNIX_H

#include <stdint.h>
#include <sys/types.h>
#include "process_ffi.h"

#ifdef __cplusplus
extern "C" {
#endif

void FreeTwoDimensionalArray(char** strArray);
char** GetCurrentProcessEnvironment(int32_t pid);

#ifdef __cplusplus
} // extern "C"
#endif
#endif // PROCESS_FFI_UNIX_H