/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * This source file is part of the Cangjie project, licensed under Apache-2.0
 * with Runtime Library Exception.
 *
 * See https://cangjie-lang.cn/pages/LICENSE for license information.
 */

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.
protected package std.deriving.api

import std.ast.*

public interface Deriving {
    prop name: String
    prop description: String

    prop supportedInterfaces: Array<QualifiedName>

    /**
     * Invoked by the deriving core to collect interface type information
     */
    func queryInterfaceInfo(interfaceName: QualifiedName): DerivingInterfaceInfo

    /**
     * Deriving implementation.
     *
     * This function is invoked once for all matched interfaces (if deriving supports multiple interfaces)
     *
     * @param input of the initial declaration
     * @param target declaration information computed by the derive core
     * @param settings contains information about options passed to deriving, usually via annotations
     * @returns token to be inserted into the resulting extend block
     */
    func derive(input: Tokens, target: DerivingTarget, logger: Logger): Tokens
}
