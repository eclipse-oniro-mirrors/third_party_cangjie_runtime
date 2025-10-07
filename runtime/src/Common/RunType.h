// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#ifndef MRT_RUN_TYPE_H
#define MRT_RUN_TYPE_H

#include "Base/Panic.h"

namespace MapleRuntime {
// slot with size no more than RUN_ALLOC_SMALL_SIZE is small slot.
// small slot is allocated from cache run in thread-local buffer first.
// slot with size (RUN_ALLOC_SMALL_SIZE, RUN_ALLOC_LARGE_SIZE] is allocated from global buffer.
constexpr size_t RUN_ALLOC_SMALL_SIZE = 104;
// slot with size larger than RUN_ALLOC_LARGE_SIZE is large slot.
constexpr size_t RUN_ALLOC_LARGE_SIZE = 2016;

// this is a short cut of RUNTYPE_SIZE_TO_RUN_IDX, only works under certain configs, see TYPES def
#define RUNTYPE_FAST_RUN_IDX(size) (((size) >> 3) - 2)

struct RunType {
public:
    // this supports a maximum of (256 * 8 == 2048 byte) run
    // we need to extend this if we want to config multiple-page run
    static constexpr uint32_t MAX_NUM_OF_RUN_TYPES = 256;

    // REMEMBER TO CHANGE THIS WHEN YOU ADD/REMOVE CONFIGS
    static constexpr uint32_t NUM_OF_RUN_TYPES = 53;

    static constexpr uint32_t NUM_OF_LOCAL_TYPES = RUNTYPE_FAST_RUN_IDX(RUN_ALLOC_SMALL_SIZE) + 1;

    // REMEMBER TO CHANGE NUM_OF_RUN_TYPES WHEN YOU ADD/REMOVE CONFIGS
    // this stores a config for each kind of run (represented by an index)
    static const RunType TYPES[NUM_OF_RUN_TYPES];

    // this map maps a size ((size >> 3 - 1) to be precise) to a run config
    // this map takes 4 * MAX_NUM_OF_RUN_TYPES == 1k
    static uint32_t g_size2Idx[MAX_NUM_OF_RUN_TYPES]; // all zero-initialised

    const bool isSmall; // this kind of run is composed of small-sized slots.

    const uint8_t numPagesPerRun; // pages per run

    const uint32_t size; // slot size of this kind of run

    static void InitRunTypeMap();
};

// assume(size <= (TYPES[N_RUN_CONFIGS - 1].size << 3))
#define RUNTYPE_SIZE_TO_RUN_IDX(size) RunType::g_size2Idx[((size) >> 3) - 1]

#define RUNTYPE_RUN_IDX_TO_SIZE(idx) (RunType::TYPES[(idx)].size)

constexpr int DEFAULT_PAGE_PER_RUN = 1;
} // namespace MapleRuntime

#endif // MRT_RUN_TYPE_H
