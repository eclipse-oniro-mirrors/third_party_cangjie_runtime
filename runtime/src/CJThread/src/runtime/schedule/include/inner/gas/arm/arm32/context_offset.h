// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.

#ifndef RUNTIME_CONTEXT_OFFSET_H
#define RUNTIME_CONTEXT_OFFSET_H

#include "offset_macro.h"

#define CONTEXT_R4 0x0
#define CONTEXT_R5 0x4
#define CONTEXT_R6 0x8
#define CONTEXT_R7 0xc
#define CONTEXT_R8 0x10
#define CONTEXT_R9 0x14
#define CONTEXT_R10 0x18
#define CONTEXT_R11FP 0x1c
#define CONTEXT_R13SP 0x20
#define CONTEXT_R14LR 0x24
#define CONTEXT_R15PC 0x28

#define CONTEXT_ARM32_D8 0x2c
#define CONTEXT_ARM32_D9 0x34
#define CONTEXT_ARM32_D10 0x3c
#define CONTEXT_ARM32_D11 0x44
#define CONTEXT_ARM32_D12 0x4c
#define CONTEXT_ARM32_D13 0x54
#define CONTEXT_ARM32_D14 0x5c
#define CONTEXT_ARM32_D15 0x64

#define CONTEXT_FPSCR 0x6c

#endif // RUNTIME_CONTEXT_OFFSET_H
