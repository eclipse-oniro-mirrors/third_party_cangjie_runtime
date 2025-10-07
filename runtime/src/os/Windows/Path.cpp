// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#include "os/Path.h"

namespace MapleRuntime {
namespace Os {
CString Path::GetBaseName(const char* path)
{
    char fname[_MAX_FNAME];
    char ext[_MAX_EXT];
    if (_splitpath_s(path, nullptr, 0, nullptr, 0, fname, _MAX_FNAME, ext, _MAX_EXT)) {
        return nullptr;
    }
    return CString(fname) + CString(ext);
}

bool Path::GetRealPath(const char* path, char* resolvedPath)
{
    return _fullpath(resolvedPath, path, PATH_MAX + 1) != nullptr;
}
} // namespace Os
} // namespace MapleRuntime
