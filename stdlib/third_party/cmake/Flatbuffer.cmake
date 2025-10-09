# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

# The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.
include(ExternalProject)

set(FLATBUFFERS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/flatbuffers)
set(FLATBUFFERS_COMPILE_OPTIONS -DCMAKE_C_FLAGS=${GCC_TOOLCHAIN_FLAG} -DCMAKE_CXX_FLAGS=${GCC_TOOLCHAIN_FLAG})
if(CMAKE_HOST_WIN32)
    list(APPEND FLATBUFFERS_COMPILE_OPTIONS -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++)
else()
    list(APPEND FLATBUFFERS_COMPILE_OPTIONS -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++)
endif()

# for CloudDragon, download in Prebuild
if(NOT EXISTS ${FLATBUFFERS_SRC}/CMakeLists.txt)
    # for Jenkins
    set(REPOSITORY_PATH https://gitcode.com/openharmony/third_party_flatbuffers.git)
    message(STATUS "Set flatbuffers REPOSITORY_PATH: ${REPOSITORY_PATH}")
    set(FLATBUFFERS_DOWNLOAD_ARGS
        GIT_REPOSITORY ${REPOSITORY_PATH}
        DOWNLOAD_DIR ${FLATBUFFERS_SRC}
        GIT_TAG master
        GIT_PROGRESS ON
        GIT_CONFIG ${GIT_ARGS}
        GIT_SHALLOW ON)
endif()

ExternalProject_Add(
    flatbuffers
    ${FLATBUFFERS_DOWNLOAD_ARGS}
    SOURCE_DIR ${FLATBUFFERS_SRC}
    CMAKE_ARGS
        # no need to Build tests and install.
        -DFLATBUFFERS_BUILD_TESTS=OFF
        -DFLATBUFFERS_INSTALL=ON
        # Build only necessary targets.
        -DFLATBUFFERS_BUILD_FLATHASH=OFF
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}
        ${FLATBUFFERS_COMPILE_OPTIONS})