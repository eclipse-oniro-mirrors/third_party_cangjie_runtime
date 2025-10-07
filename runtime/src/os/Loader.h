// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#ifndef MRT_OS_LOADER_H
#define MRT_OS_LOADER_H

#include "Base/CString.h"

namespace MapleRuntime {
namespace Os {
namespace Loader {
struct BinaryInfo {
    FixedCString symbolName;
    FixedCString filePathName;
};
#ifdef _WIN64
void GetBinaryFilePath(const void* address, BinaryInfo* binInfo);
void GetSymbolName(const void* address, BinaryInfo* binInfo);
bool IsValidAddress(const void* address);
#endif
void* LoadBinaryFile(const char* libName);
int UnloadBinaryFile(void* handler);
void* FindSymbol(void* handler, const char* symbolName);
int GetBinaryInfoFromAddress(const void* address, BinaryInfo* binInfo);
}; // namespace Loader
}; // namespace Os
}; // namespace MapleRuntime
#endif // MRT_OS_LOADER_H
