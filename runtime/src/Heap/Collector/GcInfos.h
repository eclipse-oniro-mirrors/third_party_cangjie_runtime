// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#ifndef MRT_GC_DEBUGGER_H
#define MRT_GC_DEBUGGER_H

#include <algorithm>
#include <list>
#include <map>
#include <vector>

#include "Base/Log.h"
#include "Base/LogFile.h"
#include "Base/TimeUtils.h"
#include "LoaderManager.h"
#include "StackMap/StackMap.h"

#if defined(MRT_DEBUG) && (MRT_DEBUG == 1)
#define GCINFO_DEBUG (false)
#else
#define GCINFO_DEBUG (false)
#endif

namespace MapleRuntime {
#if GCINFO_DEBUG
class GCInfoNode {
public:
    enum RootType {
        REG_ROOT,
        SLOT_ROOT,
    };
    static GCInfoNode BuildNodeForTrace(Uptr startIP, Uptr ip, FrameAddress* fa)
    {
        CString time = TimeUtil::GetTimestamp();

#ifdef __APPLE__
        FuncDescRef funcDesc = MFuncDesc::GetFuncDesc(fa);
#else
        FuncDescRef funcDesc = MFuncDesc::GetFuncDesc(startIP);
#endif
        return GCInfoNode(funcDesc->GetFuncName(), time, startIP, ip, reinterpret_cast<Uptr>(fa->returnAddress));
    }
    GCInfoNode() = default;
    GCInfoNode(const CString& name, const CString& time, Uptr startIp, Uptr ip, Uptr retPC)
        : methodName(name), startPC(startIp), pc(ip), ra(retPC), timeStamp(time)
    {}
    virtual ~GCInfoNode() = default;
    virtual void DumpFrameGCInfo() const
    {
        DLOG(ENUM, "  time stamp: %s method name: %s, start ip: %p, frame pc: %p, return address: %p", timeStamp.Str(),
             methodName.Str(), startPC, pc, ra);
        DumpMapGCInfo(regRoots, "Register roots", "register: %d, root: %p  ");
        DumpMapGCInfo(slotRoots, "Slot roots", "offset: %d, root: %p  ");
        DumpMapGCInfo(invalidRegRoots, "Invalid register roots", "register id: %d, root: %p  ");
        DumpMapGCInfo(invalidSlotRoots, "Invalid slot roots", "offset: %d, root: %p  ");
    }
    template<bool isValid = true>
    void InsertRegRoot(RegisterNum reg, const BaseObject* ref)
    {
        if (isValid) {
            regRoots[reg] = ref;
        } else {
            invalidRegRoots[reg] = ref;
        }
    }
    template<bool isValid = true>
    void InsertSlotRoots(SlotBias off, const BaseObject* ref)
    {
        if (isValid) {
            slotRoots[off] = ref;
        } else {
            invalidSlotRoots[off] = ref;
        }
    }

protected:
    template<class MapType>
    static void DumpMapGCInfo(const MapType& rootMap, const CString& title, const CString& stringFormat)
    {
        constexpr size_t numPerRow = 5;
        constexpr size_t defaultCount = 0;
        DLOG(ENUM, "    %s: {", title.Str());
        size_t size = rootMap.size();
        size_t remain = size % numPerRow;
        size_t count = defaultCount;
        CString detail = "";
        for (auto x : rootMap) {
            if (count == defaultCount) {
                detail.Append("     ");
            }
            detail.Append(CString::FormatString(stringFormat.Str(), x.first, x.second));
            if (++count == numPerRow) {
                count = defaultCount;
                detail.Append("\n");
            }
        }
        if (remain != defaultCount) {
            detail.Append("\n");
        }
        detail.Append("    }\n");
        DLOG(ENUM, "%s", detail.Str());
    }

private:
    CString methodName;
    Uptr startPC;
    Uptr pc;
    Uptr ra;
    CString timeStamp;
    std::map<RegisterNum, const BaseObject*> regRoots;
    std::map<RegisterNum, const BaseObject*> invalidRegRoots;
    std::map<SlotBias, const BaseObject*> slotRoots;
    std::map<SlotBias, const BaseObject*> invalidSlotRoots;
};

class GCInfoNodeForFix : public GCInfoNode {
public:
    static GCInfoNodeForFix BuildNodeForFix(Uptr startIP, Uptr ip, FrameAddress* fa)
    {
        CString time = TimeUtil::GetTimestamp();

#ifdef __APPLE__
        FuncDescRef funcDesc = MFuncDesc::GetFuncDesc(fa);
#else
        FuncDescRef funcDesc = MFuncDesc::GetFuncDesc(startIP);
#endif
        return GCInfoNodeForFix(funcDesc->GetFuncName(), time, startIP, ip, reinterpret_cast<Uptr>(fa->returnAddress));
    }
    GCInfoNodeForFix() = default;
    GCInfoNodeForFix(const CString& name, const CString& time, Uptr startIp, Uptr ip, Uptr retPC)
        : GCInfoNode(name, time, startIp, ip, retPC)
    {}
    virtual ~GCInfoNodeForFix() = default;
    void InsertDerivedPtrRef(BasePtrType base, DerivedPtrType derived) { derivedPtrRef[base] = derived; }
    void DumpFrameGCInfo() const override
    {
        GCInfoNode::DumpFrameGCInfo();
        DumpMapGCInfo(derivedPtrRef, "derived ptr references", "base object: %#zx, derived ptr: %#zx");
    }

private:
    std::map<BasePtrType, DerivedPtrType> derivedPtrRef;
};
class CurrentGCInfo {
public:
    ~CurrentGCInfo(){};
    void PushFrameInfoForTrace(const GCInfoNode& frameGCInfo) { gcInfosForTrace.push_back(frameGCInfo); }
    void PushFrameInfoForTrace(const GCInfoNode&& frameGCInfo) { gcInfosForTrace.push_back(frameGCInfo); }

    void PushFrameInfoForFix(const GCInfoNodeForFix& frameGCInfo) { gcInfosForFix.push_back(frameGCInfo); }
    void PushFrameInfoForFix(const GCInfoNodeForFix&& frameGCInfo) { gcInfosForFix.push_back(frameGCInfo); }
    void Clear()
    {
        gcInfosForTrace.clear();
        std::vector<GCInfoNode>().swap(gcInfosForTrace);
        gcInfosForFix.clear();
        std::vector<GCInfoNodeForFix>().swap(gcInfosForFix);
    }
    void DumpFrameInfo() const
    {
        DLOG(ENUM, "  fix roots info:");
        std::for_each(gcInfosForFix.begin(), gcInfosForFix.end(),
                      [](const GCInfoNodeForFix& info) { info.DumpFrameGCInfo(); });
        DLOG(ENUM, "  trace roots info:");
        std::for_each(gcInfosForTrace.begin(), gcInfosForTrace.end(),
                      [](const GCInfoNode& info) { info.DumpFrameGCInfo(); });
    }

private:
    std::vector<GCInfoNode> gcInfosForTrace;
    std::vector<GCInfoNodeForFix> gcInfosForFix;
};
class GCInfos {
public:
    GCInfos() = default;
    ~GCInfos() = default;
    void CreateCurrentGCInfo()
    {
        constexpr size_t numLimit = 10;
        if (gcInfos.size() >= numLimit) {
            auto& front = gcInfos.front();
            front.Clear();
            gcInfos.pop_front();
        }
        gcInfos.push_back(CurrentGCInfo());
    }
    void PushFrameInfoForTrace(const GCInfoNode& frameGCInfo) { GetCurrentGCInfo().PushFrameInfoForTrace(frameGCInfo); }
    void PushFrameInfoForTrace(const GCInfoNode&& frameGCInfo)
    {
        GetCurrentGCInfo().PushFrameInfoForTrace(frameGCInfo);
    }

    void PushFrameInfoForFix(const GCInfoNodeForFix& infoNodeFoxFix)
    {
        GetCurrentGCInfo().PushFrameInfoForFix(infoNodeFoxFix);
    }
    void PushFrameInfoForFix(const GCInfoNodeForFix&& infoNodeForFix)
    {
        GetCurrentGCInfo().PushFrameInfoForFix(infoNodeForFix);
    }

    void DumpGCInfos() const
    {
        size_t size = gcInfos.size();
        DLOG(ENUM, " current thread happened %d times GC", size);
        size_t i = 1;
        std::for_each(gcInfos.rbegin(), gcInfos.rend(), [&i](const CurrentGCInfo& cur) {
            DLOG(ENUM, " the %d scan stack: ", i++);
            cur.DumpFrameInfo();
        });
    }

private:
    CurrentGCInfo& GetCurrentGCInfo()
    {
        if (UNLIKELY(gcInfos.empty())) {
            CreateCurrentGCInfo();
        }
        return gcInfos.back();
    }
    std::list<CurrentGCInfo> gcInfos;
};
#endif
} // namespace MapleRuntime
#endif // MRT_GC_DEBUGGER_H
