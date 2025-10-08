// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#ifndef MRT_NETPOLL_H
#define MRT_NETPOLL_H

#include <pthread.h>
#include <sys/epoll.h>
#include "netpoll_common.h"
#include "macro_def.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

/* Readable events of the epoll type (including disconnection and error events) */
#define NETPOLL_READ_EVENT (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR)
/* Writable events of the epoll type (including disconnection and error events) */
#define NETPOLL_WRITE_EVENT (EPOLLOUT | EPOLLHUP | EPOLLERR)

#define EPOLL_CAPACITY 1024

typedef int FdHandle;

typedef void *NetpollFd;

/* Prototypes of epoll-related functions */
typedef int (*NetpollCreateFn)(int size);

typedef int (*NetpollCtlFn)(int epfd, int op, int fd, struct epoll_event *event);

typedef int (*NetpollWaitFn)(int epfd, struct epoll_event *events, int maxevents, int timeout);

typedef int (*NetpollCloseFn)(int epfd);

/**
 * @brief netpoll operation hooks. Different types of polls can be used at the bottom layer.
 */
struct NetpollOs {
    NetpollCreateFn createFn;
    NetpollCtlFn ctlFn;
    NetpollWaitFn waitFn;
    NetpollCloseFn closeFn;
};

/**
 * @brief Global management structure of netpoll operation hooks.
 */
struct NetpollRegister {
    pthread_mutex_t mutex;
    struct NetpollOs epoll;
    int inited;
};

/**
 * @brief netpoll metaData
 */
struct NetpollMetaData {
    int epfd;
};

/**
 * @brief netpoll exit
 * @param  npfd         [IN]  netpoll handle, point to NetpollMetaData。
 */
void NetpollExit(NetpollFd npfd);

/**
 * @brief register epoll
 * @par register epoll
 * @param  createFn     [IN]  netpoll create func
 * @param  ctlFn        [IN]  netpoll control func
 * @param  waitFn       [IN]  netpoll wait func
 * @param  closeFn      [IN]  netpoll close func
 * @retval #0
 * @retval #error
 */
int NetpollFnRegister(NetpollCreateFn createFn, NetpollCtlFn ctlFn, NetpollWaitFn waitFn,
                      NetpollCloseFn closeFn);

/**
 * @brief Unregister epoll
 * @par Unregister epoll
 */
void NetpollFnUnregister(void);

/**
 * @brief netpoll create
 * @retval #netpoll handle
 * @retval #NULL
 */
NetpollFd NetpollCreate(void);

/**
 * @brief Add the fd to be monitored to the netpoll.
 * @param  npfd         [IN]  netpoll handle
 * @param  fd           [IN]  fd
 * @param  data         [IN]  user data
 * @param  events       [IN] events
 * @retval #0
 * @retval #error
 */
int NetpollAdd(NetpollFd npfd, int fd, void *data, unsigned int events);

/**
 * @brief Delete the monitored fd from the netpoll.
 * @param  npfd         [IN]  netpoll handle
 * @param  fd           [IN]  fd
 * @retval #0
 * @retval #error
 */
int NetpollDel(NetpollFd npfd, int fd);

/**
 * @brief Access netpoll to obtain ready events.
 * @param  npfd         [IN]  netpoll handle
 * @param  events       [IN]  epoll events array
 * @param  maxevents    [IN]  Cache Array Size
 * @param  timeoutms    [IN]  timeout, in ms，0 indicates immediate response, 0xffffff indicates infinite waiting.
 * @retval #>=0 Number of ready events
 * @retval #-1
 */
int NetpollWait(NetpollFd npfd, struct epoll_event *events, int maxevents, int timeoutms);

/**
 * @brief Get inner epoll fd
 * @param  npfd         [IN]  netpoll handle
 * @retval inner epoll fd
 */
MRT_INLINE static int NetpollInnerFd(NetpollFd npfd)
{
    struct NetpollMetaData *meta = (struct NetpollMetaData *)npfd;
    return meta->epfd;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif
#endif /* MRT_NETPOLL_H */
