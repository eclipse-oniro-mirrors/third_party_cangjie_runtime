// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// The Cangjie API is in Beta. For details on its capabilities and limitations, please refer to the README file.


#include <pthread.h>
#include <sys/event.h>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include "log.h"
#include "netpoll_common.h"
#include "macro_def.h"
#include "netpoll.h"

NetpollFd NetpollCreate(void)
{
    struct NetpollMetaData *metaData = NetpollMetaDataInit();
    if (metaData == nullptr) {
        return nullptr;
    }
 
    metaData->kqfd = kqueue();
    if (metaData->kqfd == -1) {
        LOG_ERROR(errno, "kqueue create failed");
        free(metaData);
        return nullptr;
    }
 
    int error = fcntl(metaData->kqfd, F_SETFD, FD_CLOEXEC);
    if (error == -1) {
        LOG_ERROR(errno, "fcntl FD_CLOEXEC failed");
        return nullptr;
    }
    return metaData;
}
 
int NetpollAdd(NetpollFd npfd, int fd, void *data, unsigned int events)
{
    struct kevent event;
    struct NetpollMetaData *metaData = reinterpret_cast<struct NetpollMetaData *>(npfd);
    if (metaData == nullptr) {
        LOG_ERROR(ERRNO_NETPOLL_UNINIT, "netpoll uninited");
        return ERRNO_NETPOLL_UNINIT;
    }
 
    /* Initializes an event and adds it to kqueue.
       If the length of eventlist is 0, only events are registered and returned immediately.
    */
    EV_SET(&event, fd, events, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, data);
    int ret = kevent(metaData->kqfd, &event, 1, nullptr, 0, nullptr);
    if (event.flags & EV_ERROR) {
        LOG_ERROR(event.data, "netpoll add failed, kqfd: %d, fd: %d", metaData->kqfd, fd);
        return -1;
    }
    if (ret < 0) {
        LOG_ERROR(errno, "kevent failed, kqfd: %d, fd: %d", metaData->kqfd, fd);
        return ret;
    }
    return 0;
}
 
/* kevent will be deleted automatically from kqueue when fd is closed. */
int NetpollDel(NetpollFd npfd, int fd, unsigned int events)
{
    struct kevent event;
    struct NetpollMetaData *metaData = reinterpret_cast<struct NetpollMetaData *>(npfd);
    if (metaData == nullptr) {
        LOG_ERROR(ERRNO_NETPOLL_UNINIT, "netpoll uninited");
        return ERRNO_NETPOLL_UNINIT;
    }

    EV_SET(&event, fd, events, EV_DELETE, 0, 0, nullptr);
    int ret = kevent(metaData->kqfd, &event, 1, nullptr, 0, nullptr);
    if (event.flags & EV_ERROR) {
        LOG_ERROR(event.data, "netpoll del failed, fd: %d, event type: %u", fd, events);
        return -1;
    }
    if (ret < 0) {
        LOG_ERROR(errno, "kevent failed, fd: %d, event type: %u", fd, events);
        return ret;
    }
    return 0;
}
 
int NetpollWait(NetpollFd npfd, struct kevent *events, int maxevents, int timeoutms)
{
    int eventsNum;
    struct timespec timeout;
    struct NetpollMetaData *meta = reinterpret_cast<struct NetpollMetaData *>(npfd);
 
    if (meta == nullptr) {
        LOG_ERROR(ERRNO_NETPOLL_UNINIT, "netpoll uninited");
        return -1;
    }
 
    timeout.tv_sec = timeoutms / MSPERSECOND;
    timeout.tv_nsec = (timeoutms % MSPERSECOND) * NSPERMS;
    /* Waits for and listens to previously registered events within timeout period. */
    eventsNum = kevent(meta->kqfd, nullptr, 0, events, maxevents, &timeout);
    if (eventsNum == -1) {
        LOG_ERROR(errno, "kevent failed");
        return -1;
    }
    return eventsNum;
}
 
void NetpollExit(NetpollFd npfd)
{
    struct NetpollMetaData *metaData = reinterpret_cast<struct NetpollMetaData *>(npfd);
    if (npfd == nullptr) {
        return;
    }
    free(metaData);
}