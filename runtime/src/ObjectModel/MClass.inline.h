// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#ifndef MRT_MCLASS_INLINE_H
#define MRT_MCLASS_INLINE_H

#include <mutex>
#include "Flags.h"
#include "MClass.h"
#include "TypeInfoManager.h"

namespace MapleRuntime {
inline bool TypeTemplate::IsRawArray() const { return type == TypeKind::TYPE_KIND_RAWARRAY; }
inline bool TypeTemplate::IsVArray() const { return type == TypeKind::TYPE_KIND_VARRAY; }

inline bool TypeTemplate::IsArrayType() const
{
    return type == TypeKind::TYPE_KIND_RAWARRAY || type == TypeKind::TYPE_KIND_VARRAY;
}

inline void TypeTemplate::SetUUID(U16 id)
{
    U16 currentID = uuid.load();
    uuid.compare_exchange_strong(currentID, id, std::memory_order_relaxed);
}

inline bool TypeTemplate::IsInterface() const { return type == TypeKind::TYPE_KIND_INTERFACE; }

inline bool TypeTemplate::IsClass() const
{
    return type == TypeKind::TYPE_KIND_CLASS || type == TypeKind::TYPE_KIND_TEMP_ENUM;
}

inline bool TypeTemplate::IsNothing() const { return type == TypeKind::TYPE_KIND_NOTHING; }

inline bool TypeTemplate::IsTuple() const { return type == TypeKind::TYPE_KIND_TUPLE; }

inline bool TypeTemplate::IsEnum() const { return type == TypeKind::TYPE_KIND_ENUM; }

inline bool TypeTemplate::IsTempEnum() const { return type == TypeKind::TYPE_KIND_TEMP_ENUM; }

inline bool TypeTemplate::IsCString() const { return type == TypeKind::TYPE_KIND_CSTRING; }

inline bool TypeTemplate::IsCPointer() const { return type == TypeKind::TYPE_KIND_CPOINTER; }

inline bool TypeTemplate::IsStruct() const { return type == TypeKind::TYPE_KIND_STRUCT; }

// reference type, includes class, interface, rawarray and func.
inline bool TypeTemplate::IsRef() const { return type < 0; }

inline bool TypeTemplate::IsFunc() const { return type == TypeKind::TYPE_KIND_FUNC; }

inline bool TypeTemplate::IsCFunc() const { return type == TypeKind::TYPE_KIND_CFUNC; }

inline const char* TypeTemplate::GetName() const { return name; }

inline bool TypeTemplate::ReflectInfoIsNull() const { return reflectInfo == nullptr; }

inline EnumInfo* TypeTemplate::GetEnumInfo()
{
    if (IsEnum() || IsTempEnum()) {
        return enumInfo;
    }
    return nullptr;
}

inline const char* TypeInfo::GetName() const { return typeInfoName; }

inline MSize TypeInfo::GetInstanceSize() const { return instanceSize; }

inline MSize TypeInfo::GetComponentSize() const { return componentSize; }

inline bool TypeInfo::IsObjectType() const
{
    return type == TypeKind::TYPE_KIND_CLASS ||
           type == TypeKind::TYPE_KIND_WEAKREF_CLASS ||
           type == TypeKind::TYPE_KIND_TEMP_ENUM ||
           type == TypeKind::TYPE_KIND_FUNC;
}

inline bool TypeInfo::IsWeakRefType() const { return type == TypeKind::TYPE_KIND_WEAKREF_CLASS; }

inline bool TypeInfo::IsArrayType() const { return type == TypeKind::TYPE_KIND_RAWARRAY; }

inline bool TypeInfo::IsRawArray() const { return type == TypeKind::TYPE_KIND_RAWARRAY; }
inline bool TypeInfo::IsVArray() const { return type == TypeKind::TYPE_KIND_VARRAY; }

inline GCTib TypeInfo::GetGCTib() const { return gctib; }

inline TypeInfo* TypeInfo::GetComponentTypeInfo() const { return componentTypeInfo; }

inline bool TypeInfo::IsStructType() const
{
    return type == TypeKind::TYPE_KIND_STRUCT ||
           type == TypeKind::TYPE_KIND_TUPLE ||
           type == TypeKind::TYPE_KIND_ENUM;
}

inline bool TypeInfo::IsInterface() const { return type == TypeKind::TYPE_KIND_INTERFACE; }

inline bool TypeInfo::IsClass() const
{
    return type == TypeKind::TYPE_KIND_CLASS || type == TypeKind::TYPE_KIND_TEMP_ENUM ||
           type == TypeKind::TYPE_KIND_WEAKREF_CLASS;
}

inline bool TypeInfo::IsSyncClass() const
{
    return (flag & FLAG_FUTURE_CLASS) || (flag & FLAG_MUTEX_CLASS) ||
           (flag & FLAG_MONITOR_CLASS) || (flag & FLAG_WAIT_QUEUE_CLASS);
}

inline bool TypeInfo::IsNothing() const { return type == TypeKind::TYPE_KIND_NOTHING; }

inline bool TypeInfo::IsUnit() const { return type == TypeKind::TYPE_KIND_UNIT; }

inline bool TypeInfo::IsTuple() const { return type == TypeKind::TYPE_KIND_TUPLE; }

inline bool TypeInfo::IsEnum() const { return type == TypeKind::TYPE_KIND_ENUM; }

inline bool TypeInfo::IsTempEnum() const { return type == TypeKind::TYPE_KIND_TEMP_ENUM; };

inline bool TypeInfo::IsCString() const { return type == TypeKind::TYPE_KIND_CSTRING; }

inline bool TypeInfo::IsCPointer() const { return type == TypeKind::TYPE_KIND_CPOINTER; }

inline bool TypeInfo::IsStruct() const { return type == TypeKind::TYPE_KIND_STRUCT; }

// reference type, includes class, interface, rawarray and func.
inline bool TypeInfo::IsRef() const { return type < 0; }

inline bool TypeInfo::IsFunc() const { return type == TypeKind::TYPE_KIND_FUNC; }

inline bool TypeInfo::IsFloat16() const { return type == TypeKind::TYPE_KIND_FLOAT16; }

inline bool TypeInfo::IsFloat32() const { return type == TypeKind::TYPE_KIND_FLOAT32; }

inline bool TypeInfo::IsFloat64() const { return type == TypeKind::TYPE_KIND_FLOAT64; }


inline bool TypeInfo::IsCFunc() const { return type == TypeKind::TYPE_KIND_CFUNC; }

inline bool TypeInfo::IsGenericTypeInfo() const
{
    return (typeArgsNum > 0) || IsRawArray() || IsVArray() || IsCPointer();
}

inline bool TypeInfo::IsGeneric() const
{
    return type == TypeKind::TYPE_KIND_GENERIC_TI || type == TypeKind::TYPE_KIND_GENERIC_CUSTOM;
}

inline bool TypeInfo::IsReflectUnsupportedType() const
{
    return type == TypeKind::TYPE_KIND_VARRAY ||
           type == TypeKind::TYPE_KIND_TUPLE ||
           type == TypeKind::TYPE_KIND_ENUM;
}


inline I8 TypeInfo::GetFlags() const { return flag; }

inline I8 TypeInfo::GetType() const { return type; }

inline U16 TypeInfo::GetAlign() const { return align; }

inline bool TypeInfo::IsPrimitiveType() const
{
    return type <= TypeKind::TYPE_KIND_CFUNC && type >= TypeKind::TYPE_KIND_NOTHING;
}

inline bool TypeInfo::IsVaildType() const
{
    return type < TypeKind::TYPE_KIND_MAX;
}

inline TypeInfo* TypeInfo::GetFieldType(U16 idx) const
{
    return fields[idx];
}

inline EnumInfo* TypeInfo::GetEnumInfo()
{
    if (IsEnum() || IsTempEnum()) {
        return enumInfo;
    }
    return nullptr;
}

inline bool TypeInfo::HasRefField() const
{
    if (IsArrayType()) {
        TypeInfo* componentTi = GetComponentTypeInfo();
        if (componentTi->IsRef()) {
            return true;
        } else {
            return componentTi->HasRefField();
        }
    } else {
        return static_cast<bool>(flag & FLAG_HAS_REF_FIELD);
    }
}

inline bool TypeInfo::HasFinalizer() const { return static_cast<bool>(flag & FLAG_HAS_FINALIZER); }

inline bool TypeInfo::IsFutureClass() const { return static_cast<bool>(flag & FLAG_FUTURE_CLASS); }
inline bool TypeInfo::IsMonitorClass() const { return static_cast<bool>(flag & FLAG_MUTEX_CLASS); }
inline bool TypeInfo::IsMutexClass() const { return static_cast<bool>(flag & FLAG_MONITOR_CLASS); }
inline bool TypeInfo::IsWaitQueueClass() const { return static_cast<bool>(flag & FLAG_WAIT_QUEUE_CLASS); }
inline bool TypeInfo::IsBoxClass() { return static_cast<bool>(GetModifier() & MODIFIER_BOXCLASS); }

inline bool TypeInfo::ReflectInfoIsNull() const { return reflectInfo == nullptr; }

inline TypeInfo* TypeInfo::GetSuperTypeInfo() const { return superTypeInfo; }

inline U16 TypeInfo::GetFieldNum() const { return fieldNum; }

inline U32* TypeInfo::GetFieldOffsets() const
{
    CHECK(fieldOffsets != nullptr);
    return fieldOffsets;
}

inline U16 TypeInfo::GetValidInheritNum() const { return validInheritNum; }

inline U32 TypeInfo::GetUUID()
{
    if (uuid == 0) {
        TypeInfoManager *manager = TypeInfoManager::GetInstance();
        std::lock_guard<std::recursive_mutex> lock(manager->tiMutex);
        if (uuid == 0) {
            manager->AddTypeInfo(this);
        }
        CHECK(uuid != 0);
    }
    return uuid;
}

inline U32 TypeInfo::GetClassSize() const
{
    return sizeof(TypeInfo);
}

inline EnumCtorInfo* EnumInfo::GetEnumCtor(U32 idx) const
{
    CHECK(idx < GetNumOfEnumCtor());
    EnumCtorInfo* enumCtorInfo = enumCtorInfos.GetDataRef();
    return enumCtorInfo + idx;
}

inline const char* GenericTypeInfo::GetSourceGenericName() { return tt->GetName(); }
} // namespace MapleRuntime
#endif // MRT_MCLASS_INLINE_H