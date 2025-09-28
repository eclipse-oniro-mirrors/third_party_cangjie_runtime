# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
#
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

# Find OpenSSL package and prepare OpenSSL link options if target platform supports. Cangjie project
# requires OpenSSL 1.1 or later version. This file will find OpenSSL package and check its satisifies
# our version requirement. The only exception here is OHOS. For OHOS, OpenSSL library has special
# library names which differ from the official built one. Therefore, OpenSSL library for OHOS is
# manually linked, which means that they don't have any configure checking and linkage errors (if there
# are any) will occur at build time.
# 
# Usage:
#
# include(PrepareOpenSSL)
#
# If this file is processed successfully, the following variable will be defined:
# - OPENSSL_CRYPTO_LINK_OPTIONS
# - OPENSSL_SSL_LINK_OPTIONS
#
# Other variables may not have values even this file has been processed successfully. Use with care.

if(OHOS)
    set(OPENSSL_CRYPTO_LINK_OPTIONS -lcrypto_openssl.z)
    set(OPENSSL_SSL_LINK_OPTIONS -lssl_openssl.z -lcrypto_openssl.z)
else()
    # The following variables will be restore once find_package is finished.
    set(CMAKE_PREFIX_PATH_TEMP ${CMAKE_PREFIX_PATH})
    set(CMAKE_FIND_LIBRARY_SUFFIXES_TEMP ${CMAKE_FIND_LIBRARY_SUFFIXES})
    get_property(FIND_LIBRARY_USE_LIB64_PATHS_TEMP GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS)
    # For backward compatibility, --target-lib needs also to be searched.
    foreach(target_lib ${CANGJIE_TARGET_LIB})
        get_filename_component(TARGET_LIB_PREFIX "${target_lib}" DIRECTORY)
        list(APPEND CMAKE_PREFIX_PATH "${TARGET_LIB_PREFIX}")
    endforeach()
    # On Ubuntu, find_package searches /usr first for libraries but compilers search /usr/local first for header
    # files. Because CMake omits system paths from include directories, header files used while building completely
    # depends on the compiler. As a consequence, a build may use a mixture of OpenSSL files from different versions.
    # Here we give /usr/local a higher priority than /usr by adding it into CMAKE_PREFIX_PATH.
    if(CMAKE_HOST_UNIX)
        list(APPEND CMAKE_PREFIX_PATH "/usr/local")
    endif()
    # Due to the vulnerability of OpenSSL library, we always link against shared libraries. Users are able to
    # update OpenSSL by simply replace their local OpenSSL library with a later version.
    if(MINGW)
        set(CMAKE_FIND_LIBRARY_SUFFIXES ".dll.a")
    elseif(DARWIN)
        set(CMAKE_FIND_LIBRARY_SUFFIXES ".dylib")
    else()
        set(CMAKE_FIND_LIBRARY_SUFFIXES ".so")
    endif()
    # OpenSSL library files are located in lib64 folder for x86_64 by default when it's built without configure
    # options, but lib64 folder is not a conventional library folder on Ubuntu (and FIND_LIBRARY_USE_LIB64_PATHS
    # is false when building native targets on Ubuntu). To avoid find_package errors in such cases,
    # FIND_LIBRARY_USE_LIB64_PATHS is set True here manually so lib64 folder can also be searched. From
    # CMake 3.23.0, FindOpenSSL searches lib64 by default. It could be removed if we move to later CMake versions.
    set_property(GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS True)
    find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto SSL)
    set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH_TEMP})
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES_TEMP})
    set_property(GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS ${FIND_LIBRARY_USE_LIB64_PATHS_TEMP})
    # ld does not support `-l:<filename>` passing style on darwin. It needs to be passed in a standard way.
    if(DARWIN)
        set(OPENSSL_CRYPTO_LIBRARY_LINK_OPTION -lcrypto)
        set(OPENSSL_SSL_LIBRARY_LINK_OPTION -lssl -lcrypto)
    else()
        set(OPENSSL_CRYPTO_LIBRARY_LINK_OPTION -l:$<TARGET_FILE_NAME:OpenSSL::Crypto>)
        set(OPENSSL_SSL_LIBRARY_LINK_OPTION -l:$<TARGET_FILE_NAME:OpenSSL::SSL>)
    endif()
    # Since OpenSSL package is mandatory (marked REQUIRED), it must be found here or processing will be stoped.
    # Note that we are not using OPENSSL_CRYPTO_LIBRARY and OPENSSL_SSL_LIBRARY here. OpenSSL library we are
    # going to link against may not have a soname. Linking with these variables may cause the path of library
    # hard-coded into the final binary. Using -L and -l is necessary in such cases.
    set(OPENSSL_CRYPTO_LINK_OPTIONS -L$<TARGET_FILE_DIR:OpenSSL::Crypto> ${OPENSSL_CRYPTO_LIBRARY_LINK_OPTION})
    set(OPENSSL_SSL_LINK_OPTIONS -L$<TARGET_FILE_DIR:OpenSSL::SSL> ${OPENSSL_SSL_LIBRARY_LINK_OPTION}
        -L$<TARGET_FILE_DIR:OpenSSL::Crypto> ${OPENSSL_CRYPTO_LIBRARY_LINK_OPTION})
endif()
