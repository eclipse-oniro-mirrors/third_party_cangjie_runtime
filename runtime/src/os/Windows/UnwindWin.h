// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#ifndef MRT_UNWIND_WIN_H
#define MRT_UNWIND_WIN_H

#include "Base/CString.h"
#include "Base/Types.h"
#include "Common/StackType.h"
#include "WinModuleManager.h"

namespace MapleRuntime {

enum UnwindOpCodes : uint32_t {
    PUSH_NON_VOL = 0,
    ALLOC_LARGE,
    ALLOC_SMALL,
    SET_FP_REG,
    SAVE_NON_VOL,
    SAVE_NON_VOL_FAR,
    EPILOG,
    SPARE_CODE,
    SAVE_XMM128,
    SAVE_XMM128_FAR,
    PUSH_MACH_FRAME
};

union UnwindCode {
    struct {
        uint8_t codeOffset;
        uint8_t unwindOpAndOpInfo;
    } u;
    uint16_t frameOffset;

    uint8_t GetUnwindOp() const
    {
        return u.unwindOpAndOpInfo & 0x0f; // 0f: 4bits for unwindOp
    }
    uint8_t GetOpInfo() const
    {
        return (u.unwindOpAndOpInfo >> 4) & 0x0f; // 4: 4bits for opInfo
    }
};

struct UnwindInfo {
    uint8_t versionAndFlags;
    uint8_t prologSize;
    uint8_t codesCount;
    uint8_t frameRegisterAndOffset;
    UnwindCode unwindCodes[1];

    uint8_t GetVersion() const
    {
        return versionAndFlags & 0x07; // 07: 5bits for flags
    }
    uint8_t GetFlags() const
    {
        return (versionAndFlags >> 3) & 0x1f; // 3: 3bits for version, 1f: 5bits for flags
    }
    uint8_t GetFrameRegister() const
    {
        return frameRegisterAndOffset & 0x0f; // 0f: 4bits for frameRegister
    }
    uint8_t GetFrameOffset() const
    {
        return (frameRegisterAndOffset >> 4) & 0x0f; // 4: 4bits for frameOffset
    }
};

__attribute__((__noinline__)) extern "C" void GetContextWin64(uint64_t* rip, uint64_t* rsp);
FrameInfo GetCurFrameInfo(WinModuleManager& winModuleManager, Uptr pc, Uptr sp);
FrameInfo GetCallerFrameInfo(WinModuleManager& winModuleManager, const MachineFrame& curFrame,
                             UnwindContextStatus& status);
uintptr_t GetCallerRsp(WinModuleManager& winModuleManager, const MachineFrame& curFrame);

} // namespace MapleRuntime
#endif