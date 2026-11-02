// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.

#ifndef MRT_OBJECT_FORWARDING_TABLE_H
#define MRT_OBJECT_FORWARDING_TABLE_H

#include <unordered_map>
#include "Common/BaseObject.h"

namespace MapleRuntime {

using ForwardingHashTable = std::unordered_map<BaseObject*, BaseObject*>;

struct ObjectForwardingTable {
    BaseObject* GetRoute(BaseObject* fromObj) const
    {
        auto it = hashTable.find(fromObj);
        if (it != hashTable.end()) {
            return it->second;
        }
        return nullptr;
    }

    void SetForwardingAddr(BaseObject* fromObj, BaseObject* toObj)
    {
        hashTable[fromObj] = toObj;
    }

    ForwardingHashTable hashTable;
};

} // namespace MapleRuntime
#endif // MRT_OBJECT_FORWARDING_TABLE_H
