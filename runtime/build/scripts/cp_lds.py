#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

# The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.

import sys
import shutil
import os

def copy_files_from_directory(root_path, target_dir, arch):
    # 根据架构类型选择要拷贝的目录
    if arch.lower() == 'x64':
        source_dir = root_path + '/build/lds/x86_64_linux'
    elif arch.lower() == 'arm64':
        source_dir = root_path + '/build/lds/aarch64_linux'
    else:
        print(f"Error: Unsupported architecture: {arch}")
        sys.exit(1)

    print(source_dir)
    print(root_path)
    # 检查源目录是否存在
    if not os.path.isdir(source_dir):
        print(f"Error: Source directory {source_dir} does not exist.")
        sys.exit(1)
    # 遍历 source_dir 中的所有文件和子目录
    for root, dirs, files in os.walk(source_dir):
        for name in files:
            # 构建文件的绝对路径
            absolute_path = os.path.join(root, name)
            # 构建 rsync 命令
            command = f"rsync -ua '{absolute_path}' '{target_dir}'"
            # 执行 rsync 命令
            os.system(command)


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("Usage: python script.py <target_directory> <architecture>")
        sys.exit(1)

    root_path = sys.argv[1]
    target_dir = sys.argv[2]
    arch = sys.argv[3]

    copy_files_from_directory(root_path, target_dir, arch)
