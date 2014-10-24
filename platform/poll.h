#ifndef _PLATFORM_POLL_H
#define _PLATFORM_POLL_H


#ifdef POLL_EPOLL
#include <sys/epoll.h>

struct poller {
    int epoll_fd;
};

#define WAITIN  EPOLLIN
#define WAITOUT EPOLLOUT
#define WAITERR EPOLLERR
#define WAITHUP EPOLLHUP

#else
#error "No poller available for platform"
#endif


/*
 * Perform platform-specific initialization of the poller.
 */
extern int platform_poll_init(struct poller* poller);


/*
 * Register interest in readiness events for file descriptor fd.
 */
extern int platform_poll_register(struct poller* poller, int fd,
                                  uint32_t events, void* key);


/*
 * Unregister interest in file descriptor fd.
 */
extern int platform_poll_unregister(struct poller* poller, int fd);


/*
 * Poll for events on registered file descriptors.
 */
extern int platform_poll_poll(struct poller* poller, long timeout);


#endif
