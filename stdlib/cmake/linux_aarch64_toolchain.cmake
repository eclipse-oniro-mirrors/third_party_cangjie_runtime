# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
#
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

# The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.

get_filename_component(CMAKE_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH)
include("${CMAKE_DIR}/linux_toolchain.cmake")
set(CMAKE_SYSTEM_PROCESSOR "aarch64")

if("${CMAKE_SYSTEM_NAME}" STREQUAL "${CMAKE_HOST_SYSTEM_NAME}" AND "${CMAKE_SYSTEM_PROCESSOR}" STREQUAL
                                                                   "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    set(CMAKE_CROSSCOMPILING OFF)
endif()

if(CMAKE_CROSSCOMPILING)
    add_compile_definitions(__aarch64_linux_gnu__)
    add_compile_definitions(OPENSSL_ARM64_PLATFORM)
    set(CMAKE_CXX_FLAGS
        "${CMAKE_CXX_FLAGS} -Wno-effc++ -Wno-unused-variable -Wno-missing-declarations -Wno-unused-result")
endif()
