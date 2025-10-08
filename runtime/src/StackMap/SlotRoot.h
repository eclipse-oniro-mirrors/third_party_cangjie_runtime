// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#ifndef MRT_SLOT_ROOT_H
#define MRT_SLOT_ROOT_H
#include "StackMap/StackMapTypeDef.h"
namespace MapleRuntime {
class SlotRoot {
public:
    SlotRoot() : slotBias(0) {}
    SlotRoot(SlotBias bias, BitsMapSize size, const SlotBits bits[], U32 format) : slotBias(bias),
        slotBits(bits, bits + size), slotFormat(format) {}
    SlotRoot(SlotBias bias, const std::vector<SlotBits>& slotVec, U32 format) : slotBias(bias),
        slotBits(slotVec), slotFormat(format) {}
    SlotRoot(SlotBias bias, std::vector<SlotBits>&& slotVec, U32 format) : slotBias(bias),
        slotBits(slotVec), slotFormat(format) {}
    SlotRoot(const SlotRoot& other) : slotBias(other.slotBias), slotBits(other.slotBits),
        slotFormat(other.slotFormat) {}
    SlotRoot(SlotRoot&& other) : slotBias(other.slotBias), slotBits(std::move(other.slotBits)),
        slotFormat(other.slotFormat)
    {
        other.slotBias = 0;
        std::vector<SlotBits>().swap(other.slotBits);
    }

    SlotRoot& operator=(const SlotRoot& other)
    {
        if (this == &other) {
            return *this;
        }
        slotBias = other.slotBias;
        slotBits = other.slotBits;
        slotFormat = other.slotFormat;
        return *this;
    }
    SlotRoot& operator=(SlotRoot&& other)
    {
        if (this == &other) {
            return *this;
        }
        slotBias = other.slotBias;
        slotBits = std::move(other.slotBits);
        slotFormat = other.slotFormat;
        other.slotBias = 0;
        return *this;
    }

    void VisitGCRoots(const RootVisitor& visitor, const SlotDebugVisitor& debugFunc, uintptr_t base,
                      std::list<Uptr>* rootsList = nullptr) const
    {
        if (slotFormat != PURE_COMPRESSED_STACKMAP) {
            VisitWAHGCRoots(visitor, debugFunc, base, rootsList);
            return;
        }
        for (size_t i = 0; i < slotBits.size(); ++i) {
            SlotBits bit = slotBits[i];
            for (uint32_t j = 0; bit != 0; ++j, bit >>= 1) {
                if ((bit & LOWEST_BIT) == 0) {
                    continue;
                }
                SlotBias bias = static_cast<I32>(i * BIT_SIZE + j) * BYTES_PER_SLOT + slotBias * BIAS_COEF;
                SlotAddress slot = reinterpret_cast<SlotAddress>(static_cast<intptr_t>(base) + bias);
                if (debugFunc != nullptr) {
                    debugFunc(bias, slot->object);
                }
                if (rootsList != nullptr) {
                    rootsList->push_back(reinterpret_cast<Uptr>(slot->object));
                }
                visitor(*slot);
            }
        }
    }

    ~SlotRoot() { std::vector<SlotBits>().swap(slotBits); }

private:
    void VisitWAHGCRoots(const RootVisitor& visitor, const SlotDebugVisitor& debugFunc, uintptr_t base,
        std::list<Uptr>* rootsList = nullptr) const
    {
        constexpr U32 PureValWidth = 31;
        constexpr U32 PureValBit = 1 << PureValWidth;
        constexpr U32 PureValMask = PureValBit - 1;
        constexpr U32 CompressTagBit = 1 << 30;
        constexpr U32 CompressCntMask = CompressTagBit - 1;
        SlotBias baseBias = slotBias * BIAS_COEF;

        auto VisitOneSlot = [&](I32 Idx) {
            SlotBias bias = baseBias + static_cast<I32>(Idx) * BYTES_PER_SLOT;
            SlotAddress slot = reinterpret_cast<SlotAddress>(static_cast<intptr_t>(base) + bias);
            if (debugFunc != nullptr) {
                debugFunc(bias, slot->object);
            }
            if (rootsList != nullptr) {
                rootsList->push_back(reinterpret_cast<Uptr>(slot->object));
            }
            visitor(*slot);
        };

        auto ProcessOneSlotBits = [&](SlotBits bit) {
            if (bit & PureValBit) {
                bit &= PureValMask;
                for (uint32_t j = 0; bit != 0; ++j, bit >>= 1) {
                    if ((bit & LOWEST_BIT) == 0) {
                        continue;
                    }
                    VisitOneSlot(j);
                }
                baseBias += static_cast<I32>(PureValWidth) * BYTES_PER_SLOT;
            } else {
                bool isAllRef = (bit & CompressTagBit);
                U32 bitNums = (bit & CompressCntMask) * PureValWidth;
                if (isAllRef) {
                    for (uint32_t j = 0; j < bitNums; ++j) {
                        VisitOneSlot(j);
                    }
                }
                baseBias += static_cast<I32>(bitNums) * BYTES_PER_SLOT;
            }
        };

        for (SlotBits bit : slotBits) {
            ProcessOneSlotBits(bit);
        }
    }
    SlotBias slotBias;
    std::vector<SlotBits> slotBits;
    U32 slotFormat;
    constexpr static uint32_t BIT_SIZE = 32;
    constexpr static SlotBits LOWEST_BIT = 0x1;
    constexpr static int32_t BYTES_PER_SLOT = -8;
    constexpr static int32_t BIAS_COEF = 1;
};
} // namespace MapleRuntime
#endif // ~MRT_SLOT_ROOT_H