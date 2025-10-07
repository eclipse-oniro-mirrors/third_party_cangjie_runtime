# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

# The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.

if (OHOS_FLAG MATCHES 0)
set(SRC_LIST "CjHeapData.cpp")
else ()
set(SRC_LIST
"ProfilerAgentImpl.cpp"
"HeapSnapshotJsonSerializer.cpp"
"FileStream.cpp"
"CjHeapData.cpp"
"CjAllocData.cpp"
)
endif ()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../)
add_library(Inspector STATIC ${SRC_LIST})