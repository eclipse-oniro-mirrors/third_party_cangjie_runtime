// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#ifndef MRT_CJ_HEAP_DATA_H
#define MRT_CJ_HEAP_DATA_H

#include <Common/BaseObject.h>
#include <Common/StackType.h>
#include <map>
#include <set>
#include <stack>
#include <sys/time.h>
#include "Base/CString.h"
#include "UnwindStack/GcStackInfo.h"
namespace MapleRuntime {
class CjHeapData {
public:
    CjHeapData() = default;
    explicit CjHeapData(bool fromOOM) : dumpAfterOOM(fromOOM) {}

    ~CjHeapData()
    {
        auto iter = stacktraces.begin();
        while (iter != stacktraces.end()) {
            auto recordStackInfo = iter->first;
            iter++;
            delete recordStackInfo;
            recordStackInfo = nullptr;
        }
    }

    using u1 = uint8_t;
    using u2 = uint16_t;
    using u4 = uint32_t;
    using u8 = uint64_t;
    using CjHeapDataID = u8;
    using CjHeapDataStringId = CjHeapDataID;
    using CjHeapDataStackFrameId = CjHeapDataID;
    using CjHeapDataStackTraceSerialNumber = CjHeapDataID;
    static constexpr CjHeapDataStackTraceSerialNumber kCjHeapDataNullStackTrace = 0;
    const static size_t alignment = 8;
    enum CjHeapDataTag {
        TAG_STRING_IN_UTF8 = 0x01,
        TAG_CLASS_LOAD = 0x02,
        TAG_STACK_FRAME = 0x04,
        TAG_STACK_TRACE = 0x05,
        TAG_HEAP_DUMP = 0x0c,
        TAG_START_THREAD = 0x0A,
    };

    enum DumpTag {
        TAG_ROOT_UNKNOWN = 0xFF,
        TAG_ROOT_GLOBAL = 0x01,
        TAG_ROOT_LOCAL = 0x02,
        TAG_ROOT_THREAD_OBJECT = 0x08,
        TAG_CLASS_DUMP = 0x20,
        TAG_INSTANCE_DUMP = 0x21,
        TAG_OBJECT_ARRAY_DUMP = 0x22,
        TAG_PRIMITIVE_ARRAY_DUMP = 0x23,
        TAG_STRUCT_ARRAY_DUMP = 0x24
    };

    enum BasicType {
        OBJECT = 2,
        BOOLEAN = 4,
        CHAR = 5,
        FLOAT = 6,
        DOUBLE = 7,
        BYTE = 8,
        SHORT = 9,
        INT = 10,
        LONG = 11,
    };

    struct DumpObject {
        BaseObject* obj;
        u1 tag;
        u4 threadId;
        u4 frameNum;
        CjHeapDataStringId classId;
    };

    struct DumpClass {
        TypeInfo* klass;
        CjHeapDataStringId klassId;
    };

    std::list<DumpObject> dumpObjects;
    std::map<TypeInfo*, CjHeapDataStringId> dumpClassMap;
    std::map<TypeInfo*, CjHeapDataStringId> dumpStructClassMap;

    uint32_t kCjHeapDataTime = 0;

    CString methodName;
    CString fileName;
    uint32_t lineNumber = 0;

    std::map<CString, CjHeapDataStringId> strings;

    void DumpHeap();
    bool DumpHeap(int fd);
    void WriteHeap();
    void ProcessHeap();

    void WriteFixedHeader();
    void WriteString();
    void WriteAllClassLoad();
    void WriteAllStructClassLoad();
    void WriteStackFrame(FrameInfo& frame, uint32_t frameIdx);
    void WriteStackTrace();
    void WriteRecordHeader(const u1 tag, const u4 time);
    void WriteAllObjects();
    void WriteAllClass();
    void WriteAllStructClass();
    void WriteHeapDump();
    void WriteStartThread();
    void WriteGCtibFileds(GCTib tib, bool isObject, int fieldNum);
    void WriteBitmapWordFileds(GCTib tib, bool isObject, int fieldNum);
    void WriteGCTibType(GCTib tib);
    void ProcessStacktrace(RecordStackInfo* recordStackInfo);
    void AddU1(const u1 value); // add len = 1
    void AddU2(const u2 value); // add len = 1
    void AddU4(const u4 value); // add len = 1
    void AddU8(const u8 value); // add len = 1
    void AddID(const u8 value); // add len = 1

    void ModifyLength();

    void AddU1List(const u1* value, uint8_t count);
    void AddU2List(const u2* value, uint8_t count);
    void AddU4List(const u4* value, uint8_t count);
    void AddU8List(const u8* value, uint8_t count);

    void HandleAddU1(const u1* value, uint8_t count);
    void HandleAddU2(const u2* value, uint8_t count);
    void HandleAddU4(const u4* value, uint8_t count);
    void HandleAddU8(const u8* value, uint8_t count);

    void AddStringId(CjHeapDataStringId value);

    void ProcessRootGlobal();
    void ProcessRootLocal();
    void ProcessRootThreadObject();
    void ProcessRootFinalizer();
    void ProcessHeapObject(BaseObject* roots);
    void ProcessRootClass(TypeInfo* klass);
    void ProcessStructClass(TypeInfo* klass);

    void WriteGlobalRoot(BaseObject*& obj, const u1 tag);
    void WriteUnknownRoot(BaseObject*& obj, const u1 tag);
    void WriteLocalRoot(BaseObject*& obj, const u1 tag, const u4 tid, const u1 depth);
    void WriteThreadObjectRoot(BaseObject*& obj, const u1 tag, const u4 tid, const u4 stackTraceIdx);
    void WriteObjectArray(BaseObject*& obj, const u1 tag);
    void WriteStructArray(BaseObject*& obj, const u1 tag);
    void WritePrimitiveArray(BaseObject*& obj, const u1 tag);
    void WriteInstance(BaseObject*& obj, const u1 tag);
    void WriteClass(TypeInfo* klass, CjHeapDataStringId klassId, const u1 tag);
    void WriteStructClass(TypeInfo* klass, CjHeapDataStringId klassId, const u1 tag);

    void WriteClassLoad(TypeInfo* klass, CjHeapDataStringId klassId, const u1 tag);
    void GetFrameInfo(FrameInfo frame, const u1 tag);
    void EndRecord();

    std::vector<uint8_t> buffer; // buffer 8byte vector
    uint64_t length = 0;

    CjHeapDataStringId LookupStringId(const CString& string);
    CjHeapData::CjHeapDataStringId stringId = 0x40000000;
    CjHeapData::CjHeapDataStringId threadObjectId = 0x80000000;
    std::set<TypeInfo*> roots;

    bool dumpAfterOOM = false;
    CString threadName;
    std::unordered_map<FrameInfo*, CjHeapDataStackFrameId> frames;
    std::unordered_map<RecordStackInfo*, CjHeapDataStackTraceSerialNumber> stacktraces;
    FILE* fp;
    CjHeapDataStackFrameId frameId = 0;
    CjHeapDataStackFrameId threadNameId = 0;
    u4 threadId = 0;
    CjHeapDataStackTraceSerialNumber traceSerialNum = kCjHeapDataNullStackTrace + 1;
};
} // namespace MapleRuntime
#endif