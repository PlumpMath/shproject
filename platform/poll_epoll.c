#include <platform/poll.h>
#include <scheduler.h>

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/epoll.h>


#define MAX_EVENTS 64


/*
 * Perform platform-specific initialization of the poller.
 */
int platform_poll_init(struct poller* poller) {
    assert(poller != NULL);

    poller->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (poller->epoll_fd == -1) {
        return errno;
    }

    return 0;
}


/*
 * Register interest in readiness events for file descriptor fd.
 */
int platform_poll_register(struct poller* poller, int fd, uint32_t events,
        void* key) {
    struct epoll_event event = {
        .events = events | EPOLLET,
        .data.ptr = key
    };

    return epoll_ctl(poller->epoll_fd, EPOLL_CTL_ADD, fd, &event);
}


/*
 * Unregister interest in file descriptor fd.
 */
int platform_poll_unregister(struct poller* poller, int fd) {
    return epoll_ctl(poller->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
}


/*
 * Poll for events on registered file descriptors.
 */
int platform_poll_poll(struct poller* poller, long timeout) {
    struct epoll_event events[MAX_EVENTS];
    int nfds;

    for (;;) {
        nfds = epoll_wait(poller->epoll_fd, events, MAX_EVENTS, timeout);
        if (nfds == -1 && errno == EINTR) {
            continue;
        } else if (nfds == -1) {
            return -1;
        }

        for (int i = 0; i < nfds; i++) {
            sched_poll_event(events[i].data.ptr, events[i].events);
        }

        if (nfds < MAX_EVENTS) {
            return 0;
        }
    }
}
