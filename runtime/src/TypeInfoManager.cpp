// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#include "TypeInfoManager.h"
#include "Base/CString.h"
#include "Base/MemUtils.h"
#include "ObjectModel/MClass.h"
#include "ObjectManager.inline.h"
#include "Sync/Sync.h"

namespace MapleRuntime {
void TypeGCInfo::FillTypeGCInfo(TypeInfo* ti, CString &gcTibStr, U32 &curSize)
{
    U32 ptrSize = 8;
    U16 tiAlign = ti->IsRef() ? sizeof(BaseObject*) : ti->GetAlign();
    curSize = MRT_ALIGN(curSize, tiAlign);
    if (ti->IsRef()) {
        gcTibStr[curSize / ptrSize] = '1';
    } else {
        if (ti->HasRefField()) {
            CString fieldGCTibStr = GetGCTibStr(ti);
            gcTibStr.Replace(curSize / ptrSize, fieldGCTibStr);
        }
    }
    curSize += TypeInfoManager::GetTypeSize(ti);
}

void TypeGCInfo::FillArrayTypeGCInfo(TypeInfo* ti, CString &gcTibStr, U32 &curSize)
{
    U16 eleNum = ti->GetFieldNum();
    TypeInfo* componentTi = ti->GetComponentTypeInfo();
    U16 idx = 0;
    while (idx < eleNum) {
        idx++;
        if (componentTi->IsVArray()) {
            FillArrayTypeGCInfo(componentTi, gcTibStr, curSize);
            continue;
        }
        FillTypeGCInfo(componentTi, gcTibStr, curSize);
    }
}

CString TypeGCInfo::GetGCTibStr(TypeInfo* ti)
{
    U32 ptrSize = 8;
    CString gcTibStr;
    U32 curSize = 0;
    gcTibStr = CString((ti->GetInstanceSize() + ptrSize - 1) / ptrSize, '0');
    if (!ti->HasRefField()) {
        return gcTibStr;
    }
    U16 fieldNum = ti->GetFieldNum();
    for (U16 idx = 0; idx < fieldNum; ++idx) {
        TypeInfo* fieldTi = ti->GetFieldType(idx);
        // Nothing and Unit type, size is 0
        // does not need to calculate gctib
        if (fieldTi->IsNothing() || fieldTi->IsUnit()) {
            continue;
        }
        if (fieldTi->IsVArray()) {
            FillArrayTypeGCInfo(fieldTi, gcTibStr, curSize);
        } else {
            FillTypeGCInfo(fieldTi, gcTibStr, curSize);
        }
    }
    return gcTibStr;
}

U32 TypeInfoManager::GenericTiDesc::computeHash()
{
    U32 H = 1;
    auto update = [&H](U32 val) {
        H = 31 * H + val;
    };
    U32 ttUUID = tt->GetUUID();
    if (tt->GetUUID() == 0) {
        ttUUID = TypeInfoManager::GetInstance()->GetTypeTemplateUUID(tt);
    }
    update(ttUUID);

    for (U32 idx = 0; idx < argSize; idx++) {
        TypeInfo* ti = args[idx];
        if (ti->uuid == 0) {
            TypeInfoManager::GetInstance()->AddTypeInfo(ti);
        }
        CHECK(ti->uuid != 0);
        update(ti->uuid);
    }
    return H;
}

bool TypeInfoManager::GenericTiDesc::operator==(const GenericTiDesc &other) const
{
    if (hash != other.hash) {
        return false;
    }

    U16 ttUUID = tt->GetUUID();
    U16 otherTtUUID = other.tt->GetUUID();

    if (ttUUID == 0) {
        ttUUID = TypeInfoManager::GetInstance()->GetTypeTemplateUUID(tt);
    }

    if (otherTtUUID == 0) {
        otherTtUUID = TypeInfoManager::GetInstance()->GetTypeTemplateUUID(other.tt);
    }
    if (ttUUID != otherTtUUID) {
        return false;
    }

    if (argSize != other.argSize) {
        return false;
    }

    for (U32 idx = 0; idx < argSize; idx++) {
        TypeInfo* ti = argsVector[idx];
        TypeInfo* otherTi = other.args[idx];
        U32 tiUUID = ti->GetUUID();
        if (tiUUID == 0) {
            TypeInfoManager::GetInstance()->AddTypeInfo(ti);
            tiUUID = ti->GetUUID();
        }

        U32 otherTiUUID = otherTi->GetUUID();
        if (otherTiUUID == 0) {
            TypeInfoManager::GetInstance()->AddTypeInfo(otherTi);
            otherTiUUID = otherTi->GetUUID();
        }
        if (tiUUID != otherTiUUID) {
            return false;
        }
    }
    return true;
}

TypeInfoManager TypeInfoManager::typeInfoManager;
TypeInfoManager* TypeInfoManager::GetInstance() { return &typeInfoManager; }

void TypeInfoManager::Init()
{
    NewMMap(mapMemory);
}

void TypeInfoManager::Fini()
{
    // release resources
    for (auto mTable : mTableList) {
        delete mTable;
        mTable = nullptr;
    }
    for (auto m : mmapList) {
        FreeMMap(m.first, m.second);
    }
    mmapList.clear();
}

void TypeInfoManager::NewMMap(size_t size)
{
#ifdef _WIN64
    void* start = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (start == NULL) {
        LOG(RTLOG_FATAL, "failed to initialize TypeInfoManager");
    }
#else
    void* start = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (start == MAP_FAILED) {
        LOG(RTLOG_FATAL, "failed to initialize TypeInfoManager");
    } else {
#ifndef __APPLE__
    MRT_PRCTL(start, size, "TypeInfoManager");
#endif
    }
#endif
    startAddress = reinterpret_cast<uintptr_t>(start);
    endAddress = startAddress + size;
    position = startAddress;
    mmapList.push_back(std::make_pair(startAddress, size));
}

void TypeInfoManager::FreeMMap(uintptr_t address, size_t size)
{
#ifdef _WIN64
    if (!VirtualFree(reinterpret_cast<void*>(address), 0, MEM_RELEASE)) {
        LOG(RTLOG_ERROR, "VirtualFree error for TypeInfoManager");
    }
#else
    if (munmap(reinterpret_cast<void*>(address), size) != 0) {
        LOG(RTLOG_ERROR, "munmap error for TypeInfoManager");
    }
#endif
}

void TypeInfoManager::AddTypeInfo(TypeInfo* ti)
{
    if (ti->uuid != 0) {
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(tiMutex);
    if (ti->uuid != 0) {
        return;
    }
    // Some typeinfo generated by compiler have a validInheritNum field of 0,
    // and their mTableDesc is also 0.
    if (reinterpret_cast<uintptr_t>(ti->GetMTableDesc()) == 0) {
        // 15: The most significant bit indicates whether the mTable is initialized.
        ti->validInheritNum |= 1 << 15;
    }
    if (ti->IsGenericTypeInfo() && !ti->IsVArray()) {
        GenericTiDesc* tiDesc;
        if (ti->IsRawArray() || ti->IsCPointer()) {
            TypeInfo* args[] = { ti->GetComponentTypeInfo() };
            tiDesc = GetTypeInfo(ti->GetSourceGeneric(), 1, args);
        } else {
            tiDesc = GetTypeInfo(ti->GetSourceGeneric(), ti->GetTypeArgNum(), ti->GetTypeArgs());
        }
        if (tiDesc->typeInfo != nullptr && tiDesc->typeInfo->uuid != 0) {
            ti->SetUUID(tiDesc->typeInfo->uuid);
            tiDesc->typeInfo->TryInitMTableNoLock();
            ti->SetMTableDesc(tiDesc->typeInfo->GetMTableDesc());
            ti->SetReflectInfo(tiDesc->typeInfo->GetReflectInfo());
        } else {
            ti->SetUUID(tiUUID.fetch_add(1));
            tiDesc->typeInfo = ti;
            ti->TryInitMTableNoLock();
            LoaderManager::GetInstance()->RecordTypeInfo(ti);
        }
        tiDesc->SetTypeInfoStatus(TypeInfoStatus::TYPEINFO_INITED);
        return;
    }
    const char* typeInfoName = ti->GetName();
    auto it = nonGenericTypeInfos.find(typeInfoName);
    if (it != nonGenericTypeInfos.end()) {
        TypeInfo* createdTi = it->second;
        // To avoid repeated hashmap overhead, set mTable in advance.
        if (createdTi != ti) {
            CHECK(createdTi->uuid != 0);
            ti->SetUUID(createdTi->uuid);
            createdTi->TryInitMTableNoLock();
            MTableDesc* mTableDesc = createdTi->GetMTableDesc();
            ti->SetMTableDesc(mTableDesc);
        }
        return;
    }
    nonGenericTypeInfos[ti->GetName()] = ti;
    ti->SetUUID(tiUUID.fetch_add(1));
}

U16 TypeInfoManager::GetTypeTemplateUUID(TypeTemplate* tt)
{
    if (tt->GetUUID() != 0) {
        return tt->GetUUID();
    }
    std::lock_guard<std::mutex> lock(ttMutex);
    if (tt->GetUUID() != 0) {
        return tt->GetUUID();
    }
    auto it = typeTemplates.find(tt->GetName());
    if (it != typeTemplates.end()) {
        tt->SetUUID(it->second->GetUUID());
        return tt->GetUUID();
    }
    typeTemplates.emplace(tt->GetName(), tt);
    tt->SetUUID(ttUUID.fetch_add(1));
    return tt->GetUUID();
}
TypeInfoManager::GenericTiDesc* TypeInfoManager::InsertGenericTiDesc(GenericTiDesc& desc)
{
    return genericTypeInfoDescMap.InsertGenericTiDesc(desc);
}
TypeInfoManager::GenericTiDesc* TypeInfoManager::GetGenericTiDesc(GenericTiDesc& desc)
{
    return genericTypeInfoDescMap.GetGenericTiDesc(desc);
}

TypeInfoManager::GenericTiDesc* TypeInfoManager::GenericTiDescHashMap::GetGenericTiDesc(GenericTiDesc &desc)
{
    size_t bucketIdx = desc.GetHash() % buckets.size();
    auto &bucket = buckets[bucketIdx];
    bucket.rwLock.LockRead();
    auto it = bucket.maps.find(desc.GetHash());
    if (it != bucket.maps.end()) {
        for (auto descIt = it->second.begin(); descIt != it->second.end(); ++descIt) {
            if (**descIt == desc) {
                bucket.rwLock.UnlockRead();
                return *descIt;
            }
        }
    }
    bucket.rwLock.UnlockRead();
    return nullptr;
}

TypeInfoManager::GenericTiDesc* TypeInfoManager::GenericTiDescHashMap::InsertGenericTiDesc(GenericTiDesc &desc)
{
    size_t bucketIdx = desc.GetHash() % buckets.size();
    auto &bucket = buckets[bucketIdx];
    bucket.rwLock.LockWrite();
    auto it = bucket.maps.find(desc.GetHash());
    if (it != bucket.maps.end()) {
        for (auto descIt = it->second.begin(); descIt != it->second.end(); ++descIt) {
            if (**descIt == desc) {
                bucket.rwLock.UnlockWrite();
                return *descIt;
            }
        }
    }
    GenericTiDesc* tiDesc = new (std::nothrow) GenericTiDesc(desc);
    CHECK_DETAIL(tiDesc != nullptr, "fail to allocate GenericTiDesc");
    bucket.maps[desc.GetHash()].push_back(tiDesc);
    bucket.rwLock.UnlockWrite();
    return tiDesc;
}

TypeInfoManager::GenericTiDesc* TypeInfoManager::GetTypeInfo(TypeTemplate* tt, U32 argSize, TypeInfo* args[])
{
    GenericTiDesc desc(tt, argSize, args);
    GenericTiDesc* genericTiDesc = genericTypeInfoDescMap.GetGenericTiDesc(desc);
    if (genericTiDesc == nullptr) {
        genericTiDesc = genericTypeInfoDescMap.InsertGenericTiDesc(desc);
    }
    return genericTiDesc;
}

TypeInfo* TypeInfoManager::GetOrCreateTypeInfo(TypeTemplate* tt, U32 argSize, TypeInfo* args[])
{
    auto typeInfoDesc = GetTypeInfo(tt, argSize, args);
    do {
        if (typeInfoDesc->IsNotCreated() && typeInfoDesc->tid.load() == GetTid()) {
            CreatedTypeInfo(typeInfoDesc, tt, argSize, args);
        }

        if (typeInfoDesc->IsIniting() && typeInfoDesc->tid.load() == GetTid()) {
            return typeInfoDesc->typeInfo;
        }

        if (typeInfoDesc->IsInited()) {
            return typeInfoDesc->typeInfo;
        }
    } while (true);
}

void TypeInfoManager::CreatedTypeInfoImpl(GenericTiDesc* &tiDesc, TypeTemplate* tt, U32 argSize, TypeInfo* args[])
{
    CString typeInfoName = tt->GetTypeInfoName(argSize, args);
    TypeInfo* newTypeInfo = reinterpret_cast<TypeInfo*>(Allocate(sizeof(TypeInfo)));
    size_t nameSize = typeInfoName.Length() + 1;
    uintptr_t nameAddr = Allocate(nameSize);
    MapleRuntime::MemoryCopy(nameAddr, nameSize, reinterpret_cast<uintptr_t>(typeInfoName.Str()), nameSize);
    newTypeInfo->SetName(reinterpret_cast<const char*>(nameAddr));

    newTypeInfo->SetType(tt->GetType());
    newTypeInfo->SetFlag(tt->GetFlag());
    newTypeInfo->SetValidInheritNum(tt->GetValidInheritNum());
    newTypeInfo->SetvExtensionDataStart(tt->GetvExtensionDataStart());
    newTypeInfo->SetSourceGeneric(tt);
    // genericArgs
    size_t genericArgsSize = argSize * TYPEINFO_PTR_SIZE;
    uintptr_t genericArgsAddr = Allocate(genericArgsSize);
    MapleRuntime::MemoryCopy(genericArgsAddr, genericArgsSize, reinterpret_cast<uintptr_t>(args), genericArgsSize);
    newTypeInfo->SetGenericArgs(reinterpret_cast<TypeInfo**>(genericArgsAddr));
    tiDesc->typeInfo = newTypeInfo;
    tiDesc->SetTypeInfoStatus(TypeInfoStatus::TYPEINFO_INITING);

    U16 fieldNum = tt->GetFieldNum();
    U16 typeArgNum = tt->GetTypeArgNum();
    if (tt->IsTuple() || tt->IsFunc() || tt->IsRawArray()) {
        fieldNum = argSize;
        typeArgNum = argSize;
    } else if (tt->IsVArray()) {
        LOG(RTLOG_FATAL, "Instantiate TypeInfo for VArray is not supported");
    }
    newTypeInfo->SetFieldNum(fieldNum);
    newTypeInfo->SetTypeArgNum(typeArgNum);
    if (tt->IsArrayType() || tt->IsCPointer()) {
        // args stores the component typeInfo of the array or cpointer.
        TypeInfo* componentTypeTi = args[0];
        newTypeInfo->SetComponentTypeInfo(componentTypeTi);
        AddTypeInfo(newTypeInfo);
        tiDesc->SetTypeInfoStatus(TypeInfoStatus::TYPEINFO_INITED);
        return;
    }
    U32* offsets = reinterpret_cast<U32*>(Allocate(fieldNum * sizeof(U32)));
    newTypeInfo->SetOffsets(offsets);
    newTypeInfo->SetFieldAddr(reinterpret_cast<TypeInfo**>(Allocate(fieldNum * TYPEINFO_PTR_SIZE)));
}

void TypeInfoManager::FillRemainingField(GenericTiDesc* &tiDesc, TypeTemplate* tt, U32 argSize, TypeInfo* args[])
{
    if (tt->IsArrayType()) {
        return;
    }
    TypeInfo* newTypeInfo = tiDesc->typeInfo;
    FillOffsets(newTypeInfo, tt, argSize, args);
    CalculateGCTib(newTypeInfo);
    TypeInfo* super = tt->GetSuperTypeInfo(argSize, args);
    newTypeInfo->SetSuperTypeInfo(super);
    AddTypeInfo(newTypeInfo);
    AddMTable(tt, newTypeInfo, argSize, args);
    if (tt->ReflectInfoIsNull()) {
        tiDesc->SetTypeInfoStatus(TypeInfoStatus::TYPEINFO_INITED);
        return;
    }
    if (tt->IsEnum() || tt->IsTempEnum()) {
        ParseEnumInfo(tt, argSize, args, newTypeInfo);
    } else {
        FillReflectInfo(tt, newTypeInfo);
    }
    tiDesc->SetTypeInfoStatus(TypeInfoStatus::TYPEINFO_INITED);
}

void TypeInfoManager::FillReflectInfo(TypeTemplate *tt, TypeInfo *ti)
{
    ReflectInfo* ttReflectInfo = tt->GetReflectInfo();
    if (!tt->ReflectIsEnable()) {
        ti->SetReflectInfo(ttReflectInfo);
        return;
    }
    if (ttReflectInfo == nullptr) {
        return;
    }
    size_t reflectInfoSize = sizeof(ReflectInfo);
    reflectInfoSize += ttReflectInfo->GetNumOfInstanceFieldInfos() * sizeof(DataRefOffset64<InstanceFieldInfo>);
    reflectInfoSize += ttReflectInfo->GetNumOfStaticFieldInfos() * sizeof(DataRefOffset64<StaticFieldInfo>);
    reflectInfoSize += ttReflectInfo->GetNumOfInstanceMethodInfos() * sizeof(DataRefOffset64<MethodInfo>);
    reflectInfoSize += ttReflectInfo->GetNumOfStaticMethodInfos() * sizeof(DataRefOffset64<MethodInfo>);
    uintptr_t reflectInfoAddr = Allocate(reflectInfoSize);
    MapleRuntime::MemoryCopy(reflectInfoAddr, reflectInfoSize,
        reinterpret_cast<uintptr_t>(ttReflectInfo), reflectInfoSize);
    // The fieldInfo does not need to be copied,
    // and the fieldInfo obtained directly from the reflectInfo in TypeTemplate
    ReflectInfo* tiReflectInfo = reinterpret_cast<ReflectInfo*>(reflectInfoAddr);
    tiReflectInfo->SetFieldNames(ttReflectInfo->GetFieldNames());
    tiReflectInfo->SetDeclaringGenericTypeInfo(reinterpret_cast<GenericTypeInfo*>(
            ttReflectInfo->GetDeclaringGenericTypeInfo()));
    for (U32 idx = 0; idx < ttReflectInfo->GetNumOfInstanceMethodInfos(); ++idx) {
        uintptr_t methodInfoAddr = Allocate(sizeof(MethodInfo));
        MethodInfo* ttMethodInfo = ttReflectInfo->GetInstanceMethodInfo(idx);
        MapleRuntime::MemoryCopy(methodInfoAddr, sizeof(MethodInfo),
            reinterpret_cast<uintptr_t>(ttMethodInfo), sizeof(MethodInfo));
        MethodInfo* tiMethodInfo = reinterpret_cast<MethodInfo*>(methodInfoAddr);
        tiReflectInfo->SetInstanceMethodInfo(idx, tiMethodInfo);
        tiMethodInfo->SetDeclaringTypeInfo(ti);
        tiMethodInfo->SetMethodName(ttMethodInfo->GetMethodName());
        tiMethodInfo->SetGenericParameterInfos(reinterpret_cast<Uptr>(ttMethodInfo->GetGenericParameterInfos()));
        U16 actualParamNum = ttMethodInfo->GetNumOfActualParameterInfos();
        if (actualParamNum == 0) {
            continue;
        }
        uintptr_t paramInfosAddr = Allocate(sizeof(ParameterInfo) * actualParamNum);
        Uptr ttParamInfoStart = ttMethodInfo->GetActualParameterInfos();
        MapleRuntime::MemoryCopy(paramInfosAddr, sizeof(ParameterInfo) * actualParamNum,
            ttParamInfoStart, sizeof(ParameterInfo) * actualParamNum);
        ParameterInfo* tiParamInfos = reinterpret_cast<ParameterInfo*>(paramInfosAddr);
        tiMethodInfo->SetActualParameterInfos(reinterpret_cast<Uptr>(tiParamInfos));
        for (U32 paraIdx = 0; paraIdx < actualParamNum; ++paraIdx) {
            ParameterInfo* ttParamInfo = ttMethodInfo->GetActualParameterInfo(paraIdx, true);
            ParameterInfo* tiParamInfo = tiMethodInfo->GetActualParameterInfo(paraIdx, true);
            tiParamInfo->SetName(ttParamInfo->GetName());
        }
    }

    for (U32 idx = 0; idx < ttReflectInfo->GetNumOfStaticMethodInfos(); ++idx) {
        uintptr_t methodInfoAddr = Allocate(sizeof(MethodInfo));
        MethodInfo* ttMethodInfo = ttReflectInfo->GetStaticMethodInfo(idx);
        MapleRuntime::MemoryCopy(methodInfoAddr, sizeof(MethodInfo),
            reinterpret_cast<uintptr_t>(ttMethodInfo), sizeof(MethodInfo));
        MethodInfo* tiMethodInfo = reinterpret_cast<MethodInfo*>(methodInfoAddr);
        tiReflectInfo->SetStaticMethodInfo(idx, tiMethodInfo);
        tiMethodInfo->SetDeclaringTypeInfo(ti);
        tiMethodInfo->SetMethodName(ttMethodInfo->GetMethodName());
        U16 actualParamNum = ttMethodInfo->GetNumOfActualParameterInfos();
        tiMethodInfo->SetGenericParameterInfos(reinterpret_cast<Uptr>(ttMethodInfo->GetGenericParameterInfos()));
        if (actualParamNum == 0) {
            continue;
        }
        uintptr_t paramInfosAddr = Allocate(sizeof(ParameterInfo) * actualParamNum);
        Uptr ttParamInfoStart = ttMethodInfo->GetActualParameterInfos();
        MapleRuntime::MemoryCopy(paramInfosAddr, sizeof(ParameterInfo) * actualParamNum,
            ttParamInfoStart, sizeof(ParameterInfo) * actualParamNum);
        ParameterInfo* tiParamInfos = reinterpret_cast<ParameterInfo*>(paramInfosAddr);
        tiMethodInfo->SetActualParameterInfos(reinterpret_cast<Uptr>(tiParamInfos));
        for (U32 paraIdx = 0; paraIdx < actualParamNum; ++paraIdx) {
            ParameterInfo* ttParamInfo = ttMethodInfo->GetActualParameterInfo(paraIdx, true);
            ParameterInfo* tiParamInfo = tiMethodInfo->GetActualParameterInfo(paraIdx, true);
            tiParamInfo->SetName(ttParamInfo->GetName());
        }
    }
    ti->SetReflectInfo(tiReflectInfo);
}

void TypeInfoManager::InitAnyAndObjectType()
{
    if (anyTi == nullptr) {
        auto it = nonGenericTypeInfos.find(CString("std.core:Any").Str());
        if (it != nonGenericTypeInfos.end()) {
            anyTi = it->second;
        }
    }

    if (objectTi == nullptr) {
        auto it = nonGenericTypeInfos.find(CString("std.core:Object").Str());
        if (it != nonGenericTypeInfos.end()) {
            objectTi = it->second;
        }
    }
}

void TypeInfoManager::CreatedTypeInfo(GenericTiDesc* &tiDesc, TypeTemplate* tt, U32 argSize, TypeInfo* args[])
{
    CreatedTypeInfoImpl(tiDesc, tt, argSize, args);
    if (tiDesc->IsIniting()) {
        FillRemainingField(tiDesc, tt, argSize, args);
    }
}

void TypeInfoManager::ParseEnumInfo(TypeTemplate* tt, U32 argSize, TypeInfo* args[], TypeInfo* ti)
{
#ifdef __arm__
    return;
#endif
    EnumInfo* ttEi = tt->GetEnumInfo();
    if (ttEi == nullptr || (ti->GetEnumInfo() != nullptr && ti->GetEnumInfo()->IsParsed())) {
        return;
    }
    U32 enumCtorNum = ttEi->GetNumOfEnumCtor();
    EnumInfo* enumInfo = reinterpret_cast<EnumInfo*>(Allocate(sizeof(EnumInfo)));
    MapleRuntime::MemoryCopy(reinterpret_cast<Uptr>(enumInfo), sizeof(EnumInfo),
        reinterpret_cast<uintptr_t>(ttEi), sizeof(EnumInfo));
    size_t sizeOfEnumCtors = enumCtorNum * sizeof(EnumCtorInfo);
    uintptr_t enumCtorInfoAddr = Allocate(sizeOfEnumCtors);
    MapleRuntime::MemoryCopy(enumCtorInfoAddr, sizeOfEnumCtors,
        reinterpret_cast<uintptr_t>(ttEi->GetEnumCtor(0)), sizeOfEnumCtors);
    enumInfo->SetEnumCtors(reinterpret_cast<void*>(enumCtorInfoAddr));
    for (U32 idx = 0; idx < enumCtorNum; ++idx) {
        EnumCtorInfo* ctor = enumInfo->GetEnumCtor(idx);
        ctor->SetName(ttEi->GetEnumCtor(idx)->GetName());
        void* fn = reinterpret_cast<void*>(ctor->GetCtorFn());
        if (fn == nullptr) {
            continue;
        }
        TypeInfo* enumTi = reinterpret_cast<TypeInfo*>(TypeTemplate::ExecuteGenericFunc(fn, argSize, args));
        ctor->SetTypeInfo(enumTi);
    }
    enumInfo->SetParsed();
    ti->SetEnumInfo(enumInfo);
}

void TypeInfoManager::AddMTable(TypeTemplate* tt, TypeInfo* newTypeInfo, U32 argSize, TypeInfo* args[])
{
    LoaderManager::GetInstance()->GetLoader()->VisitExtenionData(
        [argSize, args, newTypeInfo, this](ExtensionData* extensionData) {
        void* whereCondFn = reinterpret_cast<void*>(extensionData->GetWhereCondFn());
        uintptr_t matched = false;
        if (whereCondFn == nullptr) {
            matched = true;
        } else {
            // &0x1: The compiler returns the lower byte of rax, but the runtime must accept it using 64 bits.
            matched = reinterpret_cast<uintptr_t>(TypeTemplate::ExecuteGenericFunc(whereCondFn, argSize, args)) & 0x1;
        }
        if (matched) {
            TypeInfo* itf = extensionData->GetInterfaceTypeInfo(argSize, args);
            AddTypeInfo(itf);
            newTypeInfo->AddMTable(itf, extensionData->GetFuncTable());
        }
        return false;
        }, tt);
}

#ifdef __arm__
void TypeInfoManager::CalculateGCTib(TypeInfo* typeInfo)
{
    CString gcTibStr = typeGCInfo.GetGCTibStr(typeInfo);
    size_t len = gcTibStr.Length();
    GCTib gcTib;
    constexpr uint8_t alignSize = sizeof(uint32_t);
    // create StdGCTib
    U16 num = gcTibStr.Length() / alignSize;
    U16 needSpace = sizeof(U32) + sizeof(U8) * num;
    StdGCTib* stdGCTib = reinterpret_cast<StdGCTib*>(Allocate(needSpace));
    stdGCTib->nBitmapWords = num;
    U8 value = 0;
    size_t curIdx = 0;
    for (size_t idx = 0; idx < len; ++idx) {
        if (gcTibStr[idx] == '1') {
            value |= 1 << (idx % alignSize);
        }
        if ((idx + 1) % alignSize == 0) {
            stdGCTib->bitmapWords[curIdx++] = value;
            value = 0;
        }
    }
    gcTib.gctib = stdGCTib;
    typeInfo->SetGCTib(gcTib);
}
#else
void TypeInfoManager::CalculateGCTib(TypeInfo* typeInfo)
{
    CString gcTibStr = typeGCInfo.GetGCTibStr(typeInfo);
    size_t len = gcTibStr.Length();
    GCTib gcTib;
    constexpr uint8_t bitmapWordLength = 64;
    constexpr uint8_t alignSize = sizeof(uint64_t);
    if (len < bitmapWordLength) {
        U64 value = 0;
        for (size_t idx = 0; idx < len; ++idx) {
            if (gcTibStr[idx] == '1') {
                value |= (U64)1 << idx;
            }
        }
        value |= (U64)1 << (bitmapWordLength - 1);
        gcTib.tag = value;
        typeInfo->SetGCTib(gcTib);
    } else {
        // create StdGCTib
        U16 num = gcTibStr.Length() / alignSize;
        U16 needSpace = sizeof(U32) + sizeof(U8) * num;
        StdGCTib* stdGCTib = reinterpret_cast<StdGCTib*>(Allocate(needSpace));
        stdGCTib->nBitmapWords = num;
        U8 value = 0;
        size_t curIdx = 0;
        for (size_t idx = 0; idx < len; ++idx) {
            if (gcTibStr[idx] == '1') {
                value |= 1 << (idx % alignSize);
            }
            if ((idx + 1) % alignSize == 0) {
                stdGCTib->bitmapWords[curIdx++] = value;
                value = 0;
            }
        }
        gcTib.gctib = stdGCTib;
        typeInfo->SetGCTib(gcTib);
    }
}
#endif

void TypeInfoManager::FillOffsets(TypeInfo* newTypeInfo, TypeTemplate* tt, U32 argSize, TypeInfo* args[])
{
    U32 instanceSize = 0;
    U8 align = 1; // If the field is empty, align is 1.
    bool hasRefField = false;
    U32 offset = 0;
    U32* offsets = newTypeInfo->GetFieldOffsets();
    if (tt->IsTuple() || tt->IsFunc()) {
        for (U32 fieldIdx = 0; fieldIdx < argSize; ++fieldIdx) {
            TypeInfo* fieldTi = args[fieldIdx];
            newTypeInfo->SetFieldType(fieldIdx, fieldTi);
            // Nothing and Unit type, size is 0
            if (fieldTi->IsNothing() || fieldTi->IsUnit()) {
                continue;
            }
            if (fieldTi->IsRef() || fieldTi->HasRefField()) {
                hasRefField = true;
            }
            U8 fieldTiAlign = fieldTi->IsRef() ? sizeof(BaseObject*) : fieldTi->GetAlign();
            offset = MRT_ALIGN(offset, fieldTiAlign);
            offsets[fieldIdx] = offset;
            align = align > fieldTiAlign ? align : fieldTiAlign;
            offset += TypeInfoManager::GetTypeSize(fieldTi);
        }
    } else {
        U32 fieldNum = newTypeInfo->GetFieldNum();
        for (U16 fieldIdx = 0; fieldIdx < fieldNum; ++fieldIdx) {
            TypeInfo* fieldTi = tt->GetFieldTypeInfo(fieldIdx, argSize, args);
            newTypeInfo->SetFieldType(fieldIdx, fieldTi);
            // Nothing and Unit type, size is 0
            if (fieldTi->IsNothing() || fieldTi->IsUnit()) {
                continue;
            }
            if (fieldTi->IsRef() || fieldTi->HasRefField()) {
                hasRefField = true;
            }
            U8 fieldTiAlign = fieldTi->IsRef() ? sizeof(BaseObject*) : fieldTi->GetAlign();
            offset = MRT_ALIGN(offset, fieldTiAlign);
            offsets[fieldIdx] = offset;
            align = align > fieldTiAlign ? align : fieldTiAlign;
            offset += TypeInfoManager::GetTypeSize(fieldTi);
        }
    }
    if (hasRefField) {
        newTypeInfo->SetFlagHasRefField();
    }
    if (newTypeInfo->IsRef()) {
        newTypeInfo->SetAlign(TYPEINFO_PTR_SIZE);
    } else {
        newTypeInfo->SetAlign(align);
    }
    instanceSize = MRT_ALIGN(offset, align);
    if (newTypeInfo->IsSyncClass()) {
        newTypeInfo->SetInstanceSize(CJFuture::SYNC_OBJECT_SIZE - 8); // the size of sync object payload
    } else {
        newTypeInfo->SetInstanceSize(instanceSize);
    }
}

U32 TypeInfoManager::GetTypeSize(TypeInfo* ti)
{
    I8 type = ti->GetType();
    switch (type) {
        case TypeKind::TYPE_KIND_NOTHING:
        case TypeKind::TYPE_KIND_UNIT:
            return 0;
        case TypeKind::TYPE_KIND_BOOL:
        case TypeKind::TYPE_KIND_RUNE:
        case TypeKind::TYPE_KIND_UINT8:
        case TypeKind::TYPE_KIND_UINT16:
        case TypeKind::TYPE_KIND_UINT32:
        case TypeKind::TYPE_KIND_UINT64:
        case TypeKind::TYPE_KIND_UINT_NATIVE:
        case TypeKind::TYPE_KIND_INT8:
        case TypeKind::TYPE_KIND_INT16:
        case TypeKind::TYPE_KIND_INT32:
        case TypeKind::TYPE_KIND_INT64:
        case TypeKind::TYPE_KIND_INT_NATIVE:
        case TypeKind::TYPE_KIND_FLOAT16:
        case TypeKind::TYPE_KIND_FLOAT32:
        case TypeKind::TYPE_KIND_FLOAT64:
        case TypeKind::TYPE_KIND_CSTRING:
        case TypeKind::TYPE_KIND_CPOINTER:
        case TypeKind::TYPE_KIND_CFUNC: {
            return ti->GetInstanceSize();
        }
        case TypeKind::TYPE_KIND_CLASS:
        case TypeKind::TYPE_KIND_EXPORTED_REF:
        case TypeKind::TYPE_KIND_FOREIGN_PROXY:
        case TypeKind::TYPE_KIND_WEAKREF_CLASS:
        case TypeKind::TYPE_KIND_INTERFACE:
        case TypeKind::TYPE_KIND_TEMP_ENUM:
        case TypeKind::TYPE_KIND_RAWARRAY:
        case TypeKind::TYPE_KIND_FUNC: {
            return sizeof(BaseObject*);
        }
        case TypeKind::TYPE_KIND_ENUM: {
            U16 fieldNum = ti->GetFieldNum();
            // The FE is to record the empty struct, and set instanceSize to 1.
            if (fieldNum == 0) {
                return ti->GetInstanceSize();
            }
            U32 totalSize = 0;
            for (U16 idx = 0; idx < fieldNum; ++idx) {
                TypeInfo* fieldTi = ti->GetFieldType(idx);
                // skip Nothing and Unit type, because size is 0
                if (fieldTi->IsNothing() || fieldTi->IsUnit()) {
                    continue;
                }
                U8 fieldTiAlign = fieldTi->IsRef() ? sizeof(BaseObject*) : fieldTi->GetAlign();
                totalSize = MRT_ALIGN(totalSize, fieldTiAlign);
                totalSize += GetTypeSize(fieldTi);
            }
            return MRT_ALIGN(totalSize, ti->GetAlign());
        }
        case TypeKind::TYPE_KIND_TUPLE:
        case TypeKind::TYPE_KIND_STRUCT: {
            U16 fieldNum = ti->GetFieldNum();
            U32 totalSize = 0;
            for (U16 idx = 0; idx < fieldNum; ++idx) {
                TypeInfo* fieldTi = ti->GetFieldType(idx);
                // skip Nothing and Unit type, because size is 0
                if (fieldTi->IsNothing() || fieldTi->IsUnit()) {
                    continue;
                }
                U8 fieldTiAlign = fieldTi->IsRef() ? sizeof(BaseObject*) : fieldTi->GetAlign();
                totalSize = MRT_ALIGN(totalSize, fieldTiAlign);
                totalSize += GetTypeSize(fieldTi);
            }
            return MRT_ALIGN(totalSize, ti->GetAlign());
        }
        case TypeKind::TYPE_KIND_VARRAY: {
            U16 fieldNum = ti->GetFieldNum();
            TypeInfo* componentTi = ti->GetComponentTypeInfo();
            U16 align = componentTi->GetAlign();
            U32 totalSize = 0;
            for (U16 idx = 0; idx < fieldNum; ++idx) {
                totalSize = MRT_ALIGN(totalSize, align);
                totalSize += GetTypeSize(componentTi);
            }
            return MRT_ALIGN(totalSize, align);
        }
        default:
            LOG(RTLOG_FATAL, "TypeInfoManager::GetTypeSize no type matched type: %d", type);
    }
    return 0;
}

uintptr_t TypeInfoManager::Allocate(size_t size)
{
    uintptr_t addr = position.fetch_add(size);
    if (addr + size > endAddress) {
        NewMMap(mapMemory);
        addr = position.fetch_add(size);
    }
    return addr;
}
} // namespace MapleRuntime
