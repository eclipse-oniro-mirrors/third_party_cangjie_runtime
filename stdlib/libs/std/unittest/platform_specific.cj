/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * This source file is part of the Cangjie project, licensed under Apache-2.0
 * with Runtime Library Exception.
 *
 * See https://cangjie-lang.cn/pages/LICENSE for license information.
 */

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.
package std.unittest

import std.process.Process

@When[os == "Windows"]
extend Process {
    static prop currentArgs: Array<String> {
        get() { Process.current.arguments }
    }
}

@When[os != "Windows"]
extend Process {
    static prop currentArgs: Array<String> {
        get() {
            var cmdl = Process.current.commandLine
            cmdl[1..cmdl.size]
        }
    }
}
