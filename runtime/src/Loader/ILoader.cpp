// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#include "ILoader.h"

#include "CjFileLoader/CjFileLoader.h"
namespace MapleRuntime {
ILoader* ILoader::CreateLoader()
{
    auto loader = new (std::nothrow) CJFileLoader();
    CHECK_DETAIL(loader != nullptr, "new loader fail");
    return loader;
}
} // namespace MapleRuntime
