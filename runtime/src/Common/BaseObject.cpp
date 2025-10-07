// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#include "BaseObject.h"

#include "Heap/Allocator/RegionInfo.h"
#include "Heap/Collector/FinalizerProcessor.h"
#include "ObjectModel/MArray.h"
#include "ObjectModel/MArray.inline.h"
#include "ObjectModel/MObject.h"
#include "ObjectModel/MObject.inline.h"

namespace MapleRuntime {
TypeInfo* BaseObject::GetTypeInfo() const
{
    StateWord stateWord = GetStateWord();
    return stateWord.GetTypeInfo();
}

#if defined(MRT_DEBUG) && (MRT_DEBUG == 1)
void BaseObject::DumpObject(int logtype, bool isSimple) const
{
    static constexpr size_t DUMP_WORDS_PER_LINE = 4;
    size_t objSize = GetSize();

    DLOG(LogType(logtype), "[obj] %p %p %zu", this, GetTypeInfo(), objSize);
    if (isSimple) {
        return;
    }
    // dump more details
    size_t word = RoundUp(objSize, sizeof(void*)) / sizeof(void*);
    MAddress obj = reinterpret_cast<MAddress>(this);
    constexpr size_t bufferSize = 256;
    char buf[bufferSize];
    for (size_t i = 0; i < word; i += DUMP_WORDS_PER_LINE) {
        int index = sprintf_s(buf, sizeof(buf), "%zx: ", (obj + (i * sizeof(MAddress))));
        size_t bound = (i + DUMP_WORDS_PER_LINE) < word ? (i + DUMP_WORDS_PER_LINE) : word;
        for (size_t j = i; j < bound; j++) {
            index += sprintf_s(buf + index, sizeof(buf) - static_cast<size_t>(index), "%p ",
                               *reinterpret_cast<ObjectPtr*>(obj + (j * sizeof(MAddress))));
#ifdef USE_32BIT_REF
            index += sprintf_s(buf + index, sizeof(buf) - static_cast<size_t>(index), "%p ",
                               *reinterpret_cast<ObjectPtr*>(obj + (j * sizeof(MAddress) + sizeof(ObjectPtr))));
#endif // USE_32BIT_REF
        }
        DLOG(LogType(logtype), buf);
    }
}
#endif

static void ForEachRefFieldInNonArrayObject(ObjectPtr obj, const RefFieldVisitor& visitor)
{
    GCTib gcTib = obj->GetGCTib();
    // gcTib record payload data, skip the TypeInfo
    MAddress objAddr = reinterpret_cast<MAddress>(obj) + sizeof(TypeInfo*);
    gcTib.ForEachBitmapWord(objAddr, visitor);
}

// Call func on each element in an object array.
static void ForEachElementInArray(ObjectPtr obj, const RefFieldVisitor& visitor)
{
    // take array length and content.
    MArray* mArray = reinterpret_cast<MArray*>(obj);
    MIndex arrayLengthVal = mArray->GetLength();
    TypeInfo* componentTypeInfo = mArray->GetComponentTypeInfo();
    if (componentTypeInfo->IsStructType()) {
        GCTib gcTib = componentTypeInfo->GetGCTib();
        MAddress contentAddr = reinterpret_cast<Uptr>(mArray) + MArray::GetContentOffset();
        for (MIndex i = 0; i < arrayLengthVal; ++i) {
            gcTib.ForEachBitmapWord(contentAddr, visitor);
            contentAddr += mArray->GetElementSize();
        }
    } else if (componentTypeInfo->IsObjectType() || componentTypeInfo->IsArrayType() ||
               componentTypeInfo->IsInterface()) {
        RefField<>* arrayContent = reinterpret_cast<RefField<>*>(mArray->ConvertToCArray());
        // for each object in array.
        for (MIndex i = 0; i < arrayLengthVal; ++i) {
            visitor(arrayContent[i]);
        }
    } else {
        LOG(RTLOG_FATAL, "array object %p has wrong component type", mArray);
    }
}

void BaseObject::ForEachRefField(const RefFieldVisitor& visitor)
{
    if (HasRefField()) {
        if (UNLIKELY(IsRawArray())) {
            ForEachElementInArray(this, visitor);
        } else {
            ForEachRefFieldInNonArrayObject(this, visitor);
        }
    }
};

void BaseObject::ForEachRefInStruct(const RefFieldVisitor& visitor, MAddress aggStart, MAddress aggEnd)
{
    if (HasRefField()) {
        if (UNLIKELY(IsRawArray())) {
            ForEachAggRefFieldInArray(visitor, aggStart, aggEnd);
        } else {
            ForEachAggRefFieldInNonArray(visitor, aggStart, aggEnd);
        }
    }
}

void BaseObject::ForEachAggRefFieldInArray(const RefFieldVisitor& visitor, MAddress aggStart, MAddress aggEnd)
{
    // take array length and content.
    MArray* mArray = static_cast<MArray*>(this);
    MIndex arrayLen = mArray->GetLength();
    TypeInfo* component = mArray->GetComponentTypeInfo();
    if (component->IsStructType()) {
        GCTib gcTib = component->GetGCTib();
        MAddress contentAddr = reinterpret_cast<Uptr>(this) + MArray::GetContentOffset();
        size_t contentSize = mArray->GetElementSize();
        // MIndex is enough to describe the size;
        MIndex startIndex = static_cast<MIndex>((aggStart - contentAddr) / contentSize);
        size_t alignedStart = startIndex * contentSize + contentAddr;
        MRT_ASSERT((alignedStart + contentSize) >= aggEnd, "aggregate element is not align\n");
        MAddress currentAddr = alignedStart;
        for (U64 i = startIndex; (i < arrayLen) && (currentAddr < aggEnd); ++i) {
            gcTib.ForEachBitmapWord(currentAddr, visitor);
            currentAddr += contentSize;
        }
    } else {
        LOG(RTLOG_FATAL, "this interface mustn't be invoked by array whose element is not record");
    }
}

void BaseObject::ForEachAggRefFieldInNonArray(const RefFieldVisitor& visitor, MAddress aggStart, MAddress aggEnd) const
{
    // gcTib record payload data, skip the TypeInfo
    GetGCTib().ForEachBitmapWordInRange(reinterpret_cast<MAddress>(this) + sizeof(TypeInfo*), visitor, aggStart,
                                        aggEnd);
}

size_t BaseObject::GetSize() const
{
    TypeInfo* kls = GetTypeInfo();
    if (kls->IsArrayType()) {
        const MArray* mArray = reinterpret_cast<const MArray*>(this);
        size_t size = mArray->GetMArraySize();
        return MapleRuntime::AlignUp<size_t>(size, AllocatorUtils::ALLOC_ALIGNMENT);
    } else {
        return MapleRuntime::AlignUp<size_t>(kls->GetInstanceSize() + sizeof(TypeInfo*),
                                             AllocatorUtils::ALLOC_ALIGNMENT);
    }
}

void BaseObject::OnFinalizerCreated()
{
    Heap& heap = Heap::GetHeap();
    // every newly-created object is marked during gc for mark-sweep gc.
    heap.GetCollector().MarkNewObject(this);
    heap.GetFinalizerProcessor().RegisterFinalizer(this);
}

bool BaseObject::IsInTraceRegion() const
{
    RegionInfo* region = RegionInfo::GetRegionInfoAt(reinterpret_cast<Uptr>(this));
    MRT_ASSERT(region != nullptr, "region is nullptr");
    return region->IsTraceRegion();
}

bool BaseObject::CompareExchangeRefField(RefField<>& field, const RefField<> oldRef, const RefField<> newRef)
{
    if (field.CompareExchange(oldRef.GetFieldValue(), newRef.GetFieldValue())) {
        DLOG(BARRIER, "update obj %p ref-field@%p: %#zx => %#zx", oldRef.GetFieldValue(), newRef.GetFieldValue());
        return true;
    }
    return false;
}
} // namespace MapleRuntime
