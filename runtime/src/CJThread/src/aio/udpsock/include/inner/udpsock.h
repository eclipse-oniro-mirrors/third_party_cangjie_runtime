// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.

#ifndef MRT_UDPSOCK_H
#define MRT_UDPSOCK_H

#include "sock.h"
#include "sock_impl.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

/**
 * @brief int udpsock module
 * @retval #0 The function is executed successfully
 * @retval #error Failed to execute the function
 */
int UdpsockInit(void);

/**
 * @brief create udp socket
 * @param domain        [IN] domain
 * @param type          [IN] type
 * @param protocol      [IN] protocol
 * @param socketError   [OUT] error
 * @retval #-1
 * @retval #socket handle
 */
SignedSocket UdpsockCreate(int domain, int type, int protocol, int *socketError);

/**
 * @brief bind socket for udp socket
 * @param fd            [IN] socket handle
 * @param addr          [IN] address
 * @param addrLen       [IN] address length
 * @retval #0 The function is executed successfully
 * @retval #error Failed to execute the function
 */
int UdpsockBind(SignedSocket fd, const struct sockaddr *addr, socklen_t addrLen);

/**
 * @brief create and bind for udp socket
 * @param sockFd        [IN] socket handle
 * @param addr          [IN] address
 * @param backlog       [IN] unuse for udp
 * @retval #0 The function is executed successfully
 * @retval #error Failed to execute the function
 */
int UdpsockBindNoListen(SignedSocket sockFd, const struct SockAddr *addr, int backlog);

/**
 * @brief connect udp socket
 * @attention UDP calls to connect do not have a three-way handshake, but only record
 *            the IP address and port of the other end, which will be immediately returned.
 *            The UDP socket that has been connected has the following changes:
 *            1.use send instead of sendto. If using sendto, the already bound address will be ignored.
 *            2.use recv instead of recvfrom. The socket can only accept data from the socket bound to connect.
 *            3.Asynchronous errors caused by a UDP socket connection will be returned to the process
 *              where it is located.
 *            4.udp sokcet can call connect multiple times to specify a new IP address and port.
 * @param fd            [IN] socket handle
 * @param addr          [IN] address
 * @param addrLen       [IN] address length
 * @retval #0 The function is executed successfully
 * @retval #error Failed to execute the function
 */
int UdpsockConnect(SignedSocket fd, const struct sockaddr *addr, socklen_t addr_len);

/**
 * @brief bind and connect for udp socket
 * @param connFd        [IN] socket handle
 * @param local         [IN] bind address
 * @param peer          [IN] connect address
 * @param timeout       [IN] unuse for udp
 * @retval #0 The function is executed successfully
 * @retval #error Failed to execute the function
 */
int UdpsockBindConnect(SignedSocket connFd, struct SockAddr *local, struct SockAddr *peer, unsigned long long timeout);

/**
 * @brief disconnect udp socket
 * @param connFd        [IN] socket handle
 * @retval #0 The function is executed successfully
 * @retval #error Failed to execute the function
 */
int UdpsockDisconnect(SignedSocket connFd);

#ifdef MRT_MACOS
/**
 * @brief disconnect udp socket for ipv6
 * @param connFd        [IN] socket handle
 * @retval #0 The function is executed successfully
 * @retval #error Failed to execute the function
 */
int UdpsockDisconnectForIPv6(SignedSocket connFd);
#endif

/**
 * @brief register socket hooks
 * @param hooks         [OUT] SockCommHooks pointer
 */
void UdpsockRegisterSocketHooks(struct SockCommHooks *hooks);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif
#endif /* MRT_UDPSOCK_H */
