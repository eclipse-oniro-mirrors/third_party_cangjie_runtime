// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.

#ifndef MRT_CJTHREAD_SYSCALL_COMMON_H
#define MRT_CJTHREAD_SYSCALL_COMMON_H

#include <unistd.h>
#include <fcntl.h>
#include "mid.h"
#ifdef MRT_WINDOWS
#include <ws2tcpip.h>
#else
#include <stdlib.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/syscall.h>
#include <poll.h>
#include <netdb.h>
#endif  /* MRT_WINDOWS */

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

/**
 * @brief syscall open
 * @par Open the file and obtain the specified permissions.
 * The caller ensures the legality of the path on their own.
 * @param  filename     [IN]  file path
 * @param  flags        [OUT] file operation permissions
 * @param  mode         [IN]  permission to create files
 * @retval Returns 0 upon success, -1 upon failure, and errno is set as the error code.
 */
int SyscallOpen(const char *filename, int flags, unsigned short mode);

/**
 * @brief syscall close
 * @par Close fd
 * @param  fd           [IN]  fd
 * @retval Returns 0 upon success, -1 upon failure, and errno is set as the error code.
 */
int SyscallClose(int fd);

/**
 * @brief syscall read
 * @par read
 * @param  fd           [IN] fd
 * @param  buf          [OUT] buffer
 * @param  count        [IN]  read size
 * @retval Returns length upon success, -1 upon failure, and errno is set as the error code.
 */
int SyscallRead(int fd, void *buf, size_t count);

/**
 * @brief syscall write
 * @par write
 * @param  fd           [IN]  fd
 * @param  buf          [OUT] buffer
 * @param  count        [IN]  write size
 * @retval Returns length upon success, -1 upon failure, and errno is set as the error code.
 */
int SyscallWrite(int fd, const void *buf, size_t count);

/**
 * @brief sysacll sleep
 * @par sleep
 * @param  seconds      [IN]  duration
 * @retval Returns 0 upon success.
 */
unsigned int SyscallSleep(unsigned int seconds);

/**
 * @brief syscall usleep
 * @par usleep
 * @param  usecs        [IN]  duration
 * @retval Returns 0 upon success.
 */
int SyscallUsleep(unsigned int usecs);

/**
* @brief Encapsulate glibc interface getaaddrinfo
* @par Obtain one or more address information based on host and service
* @param  node          [IN]  A host name or address
* @param  service       [IN]  service name
* @param  hints         [IN]  Expected type of information to be returned
* @param  res           [OUT] One or more address information to be returned
* @retval Returns 0 upon success.
*/
int SyscallGetaddrinfo(const char *node, const char *service, const struct addrinfo *hints,
                       struct addrinfo **res);

/**
 * @brief Encapsulate glibc interface getnameinfo
 * @par Convert socket address to host and service
 * @param  sa           [IN]  socket address
 * @param  salen        [IN]  socket address length
 * @param  host         [OUT] host name
 * @param  hostlen      [IN]  host name length
 * @param  serv         [OUT] service name
 * @param  servlen      [IN]  service name length
 * @param  flags        [IN]  option
 * @retval Returns 0 upon success.
 */
int SyscallGetnameinfo(const struct sockaddr *sa, socklen_t salen, char *host, socklen_t hostlen,
                       char *serv, socklen_t servlen, int flags);

/**
 * @brief Encapsulate glibc interface freeaddrinfo
 * @par Release target addrinfo
 * @param  res          [IN]  Address information to be released
 */
void SyscallFreeaddrinfo(struct addrinfo *res);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif
#endif /* MRT_CJTHREAD_SYSCALL_COMMON_H */