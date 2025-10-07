// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#ifndef MRT_CYCLEQUEUE_H_
#define MRT_CYCLEQUEUE_H_

#include <queue>

#include "Base/Log.h"
#include "Base/Macros.h"
#include "Base/Types.h"

namespace MapleRuntime {
template<typename T>
class CycleQueue {
public:
    CycleQueue() : frontIndex(0), rearIndex(0) {}

    ~CycleQueue()
    {
        if (UNLIKELY(queueBuffer != nullptr)) {
            delete queueBuffer;
            queueBuffer = nullptr;
        }
    }

    inline bool Empty() const { return (queueBuffer == nullptr) ? EmptyArray() : EmptyQueue(); }

    inline void Push(T ele)
    {
        if (LIKELY(queueBuffer == nullptr)) {
            if (PushArray(ele)) {
                return;
            } else {
                // cycle array is full, move to std::queue
                CopyElementToQueue();
            }
        }
        PushQueue(ele);
    }

    inline void Pop()
    {
        if (LIKELY(queueBuffer == nullptr)) {
            PopArray();
        } else {
            PopQueue();
        }
    }

    inline T Front() { return (queueBuffer == nullptr) ? FrontArray() : FrontQueue(); }

    static constexpr uint16_t MAX_SIZE = 30;
    uint16_t frontIndex;
    uint16_t rearIndex;
    T arrayBuffer[MAX_SIZE] = { nullptr }; // level 1 cache
    std::queue<T>* queueBuffer = nullptr;  // level 2 cache

private:
    inline bool EmptyArray() const { return frontIndex == rearIndex; }

    inline bool PushArray(T ele)
    {
        uint16_t tmpRear = (rearIndex + 1) % MAX_SIZE;
        if (UNLIKELY(frontIndex == tmpRear)) {
            return false;
        }
        arrayBuffer[rearIndex] = ele;
        rearIndex = tmpRear;
        return true;
    }

    // just adjust index, no pop value
    inline void PopArray() { frontIndex = (frontIndex + 1) % MAX_SIZE; }

    inline T FrontArray() { return arrayBuffer[frontIndex]; }

    inline bool EmptyQueue() const { return queueBuffer->empty(); }

    inline void PushQueue(T ele) { queueBuffer->push(ele); }

    inline void PopQueue() { queueBuffer->pop(); }

    inline T FrontQueue() { return queueBuffer->front(); }

    inline void CopyElementToQueue()
    {
        queueBuffer = new (std::nothrow) std::queue<T>;
        CHECK_DETAIL(queueBuffer != nullptr, "queueBuffer is nullptr");
        while (!EmptyArray()) {
            PushQueue(FrontArray());
            PopArray();
        }
    }
};
} // namespace MapleRuntime

#endif // MRT_CYCLEQUEUE_H_
