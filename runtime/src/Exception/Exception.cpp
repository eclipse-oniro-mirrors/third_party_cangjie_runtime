// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#include "Exception.h"

#include "Base/LogFile.h"
#include "Base/Macros.h"
#include "StackManager.h"
#include "ExceptionCApi.h"
#include "Mutator/MutatorManager.h"
#include "UnwindStack/EhStackInfo.h"
#include "schedule.h"

namespace MapleRuntime {
// Use the prologue info in stackmap to restore the registers layer by layer.
// Return the capture point address.
extern "C" uintptr_t MRT_RestoreContext(CalleeSavedRegisterContext& context)
{
    ExceptionWrapper& eWrapper = Mutator::GetMutator()->GetExceptionWrapper();
    eWrapper.RestoreContext(context);
    auto tls = MRT_GetThreadLocalData();
#if defined(__x86_64__)
    if (context.r15 != tls) {
        context.r15 = tls;
    }
#elif defined(__aarch64__)
    if (context.x28 != tls) {
        context.x28 = tls;
    }
#endif
    return eWrapper.GetLandingPad();
}

extern "C" uintptr_t MRT_GetTopManagedPC()
{
    ExceptionWrapper& eWrapper = Mutator::GetMutator()->GetExceptionWrapper();
    return eWrapper.GetTopManagedPC();
}

void ExceptionHandling::BuildEHFrameInfo()
{
    EHStackInfo ehStackInfo;
    ehStackInfo.FillInStackTrace();
    std::vector<FrameInfo>& stackInfo = ehStackInfo.GetStack();
#if defined(MRT_DEBUG) && (MRT_DEBUG == 1)
    if (UNLIKELY(ENABLE_LOG(EXCEPTION))) {
        DLOG(EXCEPTION, "build eh stack");
        DLOG(EXCEPTION, "FrameInfos\tsize : %zu", stackInfo.size());
        DLOG(EXCEPTION, "layer\tFrameType\tFA\t\tIP\t\tStartProc\tLSDAStart\t");
        for (int i = 0; i < stackInfo.size(); ++i) {
            DLOG(EXCEPTION, "#%x\t%x\t\t%x\t%x\t0x%x\t0x%x", i, stackInfo[i].GetFrameType(),
                 stackInfo[i].mFrame.GetFA(), stackInfo[i].mFrame.GetIP(),
                 reinterpret_cast<uintptr_t>(stackInfo[i].GetStartProc()),
                 reinterpret_cast<uintptr_t>(stackInfo[i].GetLsdaProc()));
        }
    }
#endif
    std::vector<EHFrameInfo>& ehFrameInfos = eWrapper->GetEHFrameInfos();
    for (auto frame : stackInfo) {
        if (frame.GetFrameType() == FrameType::MANAGED) {
            EHFrameInfo ehFrameInfo(frame, *eWrapper);
            ehFrameInfos.push_back(ehFrameInfo);
            if (ehFrameInfo.IsCatchException()) {
                eWrapper->SetIsCaught(true);
                eWrapper->SetTypeIndex(ehFrameInfo.GetTTypeIndex() & 0xFFFFFFFF);
                eWrapper->SetLandingPad(ehFrameInfo.GetLandingPad());
                break;
            }
        } else {
            lastRuntimeFrame = frame;
        }
    }

    if (UNLIKELY(ENABLE_LOG(EXCEPTION))) {
        DLOG(EXCEPTION, "parse eh stack");
        DLOG(EXCEPTION, "EHFrameInfos size : %zu", ehFrameInfos.size());
        DLOG(EXCEPTION, "Layer\tFA\t\tIP\t\tTTpyeIndex\t\tLandingPad\tCatch\t");
        for (int i = 0; i < ehFrameInfos.size(); ++i) {
            DLOG(EXCEPTION, "#%x\t%x\t%x\t%x\t0x%p\t%x", i, ehFrameInfos[i].mFrame.GetFA(),
                 ehFrameInfos[i].mFrame.GetIP(), ehFrameInfos[i].GetTTypeIndex(),
                 reinterpret_cast<void*>(ehFrameInfos[i].GetLandingPad()),
                 ehFrameInfos[i].IsCatchException());
        }
    }
}

void ExceptionHandling::ChangeEHStackInfoLR() const
{
    std::vector<EHFrameInfo>& ehFrameInfos = eWrapper->GetEHFrameInfos();
    if (UNLIKELY(ehFrameInfos.size() == 0)) {
        LOG(RTLOG_ERROR, "There are no one managed frame in the stack block.");
        return;
    }
    if (!eWrapper->IsCaught()) {
        const uint32_t* addr = ehFrameInfos.back().mFrame.GetFA()->returnAddress;
        eWrapper->SetLandingPad(reinterpret_cast<uintptr_t>(const_cast<uint32_t*>(addr)));
    }

    eWrapper->SetTopManagedPC(
        reinterpret_cast<uintptr_t>(const_cast<uint32_t*>(lastRuntimeFrame.mFrame.GetFA()->returnAddress)));
#if defined(ENABLE_BACKWARD_PTRAUTH_CFI)
    auto mod = reinterpret_cast<uint64_t>(lastRuntimeFrame.mFrame.GetFA()) +
                GetRuntimeFrameSize((MachineFrame&)lastRuntimeFrame.mFrame);
    auto pc = PtrauthSignWithInstAkey(reinterpret_cast<Uptr>(RestoreContextForEH), reinterpret_cast<Uptr>(mod));
    lastRuntimeFrame.mFrame.GetFA()->returnAddress = reinterpret_cast<uint32_t*>(pc);
#else
    lastRuntimeFrame.mFrame.GetFA()->returnAddress = reinterpret_cast<uint32_t*>(RestoreContextForEH);
#endif
}
} // namespace MapleRuntime
