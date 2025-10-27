// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#ifndef MRT_FLAGS_H
#define MRT_FLAGS_H

// adapted from modifier.h in MapleRuntime
#include "Base/Types.h"

namespace MapleRuntime {
enum TypeKind : I8 {
    // reference type
    TYPE_KIND_CLASS = -128,
    TYPE_KIND_INTERFACE = -127,
    TYPE_KIND_RAWARRAY = -126,
    TYPE_KIND_FUNC = -125,
    TYPE_KIND_TEMP_ENUM = -124,
    TYPE_KIND_WEAKREF_CLASS = -123,
    TYPE_KIND_FOREIGN_PROXY = -122,
    TYPE_KIND_EXPORTED_REF = -121,
    TYPE_KIND_GENERIC_TI = -1,
    TYPE_KIND_GENERIC_CUSTOM = -2,

    // value type
    TYPE_KIND_NOTHING = 0,
    TYPE_KIND_UNIT,
    TYPE_KIND_BOOL,
    TYPE_KIND_RUNE,
    TYPE_KIND_UINT8,
    TYPE_KIND_UINT16 = 5,
    TYPE_KIND_UINT32,
    TYPE_KIND_UINT64,
    TYPE_KIND_UINT_NATIVE,
    TYPE_KIND_INT8,
    TYPE_KIND_INT16 = 10,
    TYPE_KIND_INT32,
    TYPE_KIND_INT64,
    TYPE_KIND_INT_NATIVE,
    TYPE_KIND_FLOAT16,
    TYPE_KIND_FLOAT32 = 15,
    TYPE_KIND_FLOAT64,
    TYPE_KIND_CSTRING,
    TYPE_KIND_CPOINTER,
    TYPE_KIND_CFUNC,
    TYPE_KIND_VARRAY = 20,
    TYPE_KIND_TUPLE,
    TYPE_KIND_STRUCT,
    TYPE_KIND_ENUM,
    TYPE_KIND_MAX,
};


constexpr U32 FLAG_HAS_REF_FIELD = 0x1 << 0; // fast path to skip scanning some object
constexpr U32 FLAG_HAS_FINALIZER = 0x1 << 1;
constexpr U32 FLAG_FUTURE_CLASS = 0x1 << 2;
constexpr U32 FLAG_MUTEX_CLASS = 0x1 << 3;
constexpr U32 FLAG_MONITOR_CLASS = 0x1 << 4;
constexpr U32 FLAG_WAIT_QUEUE_CLASS = 0x1 << 5;
constexpr U32 FLAG_REFLECTION = 0x1 << 6;

constexpr U32 MODIFIER_DEFAULT     = 0x1 << 0;
constexpr U32 MODIFIER_PRIVATE     = 0x1 << 1;
constexpr U32 MODIFIER_PROTECTED   = 0x1 << 2;
constexpr U32 MODIFIER_PUBLIC      = 0x1 << 3;
constexpr U32 MODIFIER_IMMUTABLE   = 0x1 << 4;
constexpr U32 MODIFIER_BOXCLASS    = 0x1 << 5;
constexpr U32 MODIFIER_OPEN        = 0x1 << 6;
constexpr U32 MODIFIER_OVERRIDE    = 0x1 << 7;
constexpr U32 MODIFIER_REDEF       = 0x1 << 8;
constexpr U32 MODIFIER_ABSTRACT    = 0x1 << 9;
constexpr U32 MODIFIER_SEALED      = 0x1 << 10;
constexpr U32 MODIFIER_MUT         = 0x1 << 11;
constexpr U32 MODIFIER_STATIC      = 0x1 << 12;
constexpr U32 MODIFIER_ENUM_KIND0  = 0x1 << 13;
constexpr U32 MODIFIER_ENUM_KIND1  = 0x1 << 14;
constexpr U32 MODIFIER_ENUM_KIND2  = 0x1 << 15;
constexpr U32 MODIFIER_ENUM_KIND3  = 0x1 << 16;
constexpr U32 MODIFIER_HAS_SRET0   = 0x1 << 17; // has sret but it is'not generic'
constexpr U32 MODIFIER_HAS_SRET1   = 0x1 << 18; // has sret and it is 'T'
constexpr U32 MODIFIER_HAS_SRET2   = 0x1 << 19; // has sret and it is 'known struct T'
constexpr U32 MODIFIER_HAS_SRET3   = 0x1 << 20; // has sret and it is 'unknow struct T'
constexpr U32 MODIFIER_ENUM_PARSED = 0x1 << 21;
constexpr U32 MODIFIER_INVALID     = 0x1 << 31;
} // namespace MapleRuntime

#endif // MRT_FLAGS_H
