# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

# The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.

# toolchain for linux12_x86_64

# Platform
set(PLATFORM_NAME "ios_simulator_cangjie")

# Environment info
set(OS "ios")

set(CPU_CORE "arm64")
set(CPU_FAMILY "arm")
set(CPU_TYPE "armarch8")
set(MEM_TYPE "mem")
set(BYTE_ORDER "le")
set(COMPILER "gnu")
set(FWD_PLATFORM "MCCA")

set(CMAKE_ASM_COMPILER_WORKS 1)
set(CMAKE_ASM_ABI_COMPILED 1)

# The build tools
set(CMAKE_C_COMPILER "clang")
set(CMAKE_CXX_COMPILER "clang++")

# The compile flags for common
set(CMAKE_C_FLAGS
    "-Wall \
    -Wextra \
    -Wformat=2 \
    -Wpointer-arith \
    -Wdate-time \
    -Wfloat-equal \
    -Wswitch-default \
    -Wshadow \
    -Wvla \
    -Wunused \
    -Wundef \
    -m64 \
    -fno-strict-aliasing \
    -fno-omit-frame-pointer \
    -fgnu89-inline \
    -fsigned-char \
    -fno-common \
    -fstack-protector-strong \
    -fPIC"
)

set(CMAKE_CXX_FLAGS
    "-Wall \
    -Wextra \
    -Wformat=2 \
    -Wpointer-arith \
    -Wdate-time \
    -Wfloat-equal \
    -Wswitch-default \
    -Wshadow \
    -Wvla \
    -Wunused \
    -Wundef \
    -m64 \
    -std=c++11 \
    -fno-strict-aliasing \
    -fno-omit-frame-pointer \
    -fsigned-char \
    -fno-common \
    -fstack-protector-strong \
    -fno-exceptions \
    -fPIC"
)

if("${DEBUG_INFO}" STREQUAL "INFO")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
    set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -g")
endif()

# If building stage is publish, add CMAKE_C_FLAGS
if("${BUILDING_STAGE}" STREQUAL "publish")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-strong")
    set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-strong")
endif()

if ("${BUILD_APPLE_STATIC}" STREQUAL "apple_static_lib")
    set(APPLE_STATIC_BUILD "true")
else()
    set(APPLE_STATIC_BUILD "false")
endif()

# The compile flags for debug version only
if("${BUILDING_STAGE}" STREQUAL "asan")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fsanitize-recover=address")
    set(CMAKE_C_FLAGS_DEBUG "-g")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fsanitize-recover=address")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -gdwarf-4")
else()
    set(CMAKE_C_FLAGS_DEBUG "-g -Wframe-larger-than=2048")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -Wframe-larger-than=2048 -gdwarf-4")
endif()

# The compile flags for release version only
set(CMAKE_C_FLAGS_RELEASE "-D_FORTIFY_SOURCE=2 -O2 -target arm64-apple-ios11-simulator")
set(CMAKE_CXX_FLAGS_RELEASE "-D_FORTIFY_SOURCE=2 -O2 -target arm64-apple-ios11-simulator")

set(CMAKE_C_FLAGS_RELWITHDEBINFO "-g -D_FORTIFY_SOURCE=2 -O2 -target arm64-apple-ios11-simulator")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-g -D_FORTIFY_SOURCE=2 -O2 -target arm64-apple-ios11-simulator")

# The assemble flags
set(CMAKE_ASM_FLAGS
    "${CMAKE_C_FLAGS}"
)

# Attention we need to clear CMAKE_ASM_FLAGS_DEBUG and CMAKE_ASM_FLAGS_RELEASE,
# otherwise cmake will add some default compile option which we may not want.

# The assemble flags for debug version only
set(CMAKE_ASM_FLAGS_DEBUG "-target arm64-apple-ios11-simulator")

# The assemble flags for release version only
set(CMAKE_ASM_FLAGS_RELEASE "-target arm64-apple-ios11-simulator")
set(CMAKE_ASM_FLAGS_RELWITHDEBINFO "-target arm64-apple-ios11-simulator")

# The compile definitions
add_compile_definitions(
   "_LARGEFILE_SOURCE"
   "_FILE_OFFSET_BITS=64"
   "VOS_OS_VER=VOS_LINUX"
   "VOS_HARDWARE_PLATFORM=VOS_ARM"
   "MRT_HARDWARE_PLATFORM=MRT_ARM"
   "VOS_CPU_TYPE=VOS_CORTEXA72"
   "VOS_WORDSIZE=64"
   "VOS_BYTE_ORDER=VOS_LITTLE_ENDIAN"
   "USE___THREAD"
   "$<$<CONFIG:Debug>:VOS_BUILD_DEBUG=1>"
   "$<$<CONFIG:Release>:VOS_BUILD_RELEASE=1>"
   "TLS_COMMON_DYNAMIC"
   "CANGJIE"
   "MRT_MACOS"
   "_XOPEN_SOURCE=600"
   "_DARWIN_C_SOURCE"
)

# The link flags
set(CMAKE_SHARED_LINKER_FLAGS
    "-m64 \
    -rdynamic \
    -L${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/hwsecurec/lib/ios \
    -lsecurec \
    -lpthread"
)

# The ar flags
set(CMAKE_C_CREATE_STATIC_LIBRARY "<CMAKE_AR> rcD <TARGET> <OBJECTS>")

set(IOS_PLATFORM "SIMULATOR")
set(IOS_PLATFORM_LOCATION "iPhoneSimulator.platform")
set(CMAKE_IOS_DEVELOPER_ROOT "/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer")
set(CMAKE_IOS_SDK_ROOT "/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator17.5.sdk")
set(CMAKE_OSX_SYSROOT "/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator17.5.sdk")
# The macos minimum version
set(CMAKE_OSX_DEPLOYMENT_TARGET "")
