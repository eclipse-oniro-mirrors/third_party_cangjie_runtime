#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

# The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.

import sys
import os
import shutil

def copy_files(source_dir, target_dir, include_component):
    # 检查源目录是否存在
    print(source_dir)
    if not os.path.exists(source_dir):
        print(f"ERROR: Source directory {source_dir} does not exist.")
        sys.exit(1)
    
    # 检查目标目录是否存在，如果不存在则创建
    print(target_dir)
    if not os.path.exists(target_dir):
        os.makedirs(target_dir)
    
    for component_dir in include_component:
        source_component_dir = os.path.join(source_dir, component_dir)
        command = f"rsync -ua {source_component_dir} {target_dir}"
        os.system(command)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: ohos_cp_include.py <source_dir> <target_dir> <cpu_family> <cpu_core>")
        sys.exit(1)
    source_dir = sys.argv[1]
    target_dir = sys.argv[2]
    include_component = []
    for i in range(3, len(sys.argv)):
        include_component.append(sys.argv[i])
    copy_files(source_dir, target_dir, include_component)