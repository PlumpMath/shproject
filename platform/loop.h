#ifndef _PLATFORM_LOOP_H
#define _PLATFORM_LOOP_H

#include <util/list.h>


#ifdef USE_EPOLL
#include <sys/epoll.h>

typedef struct event_loop {
    int epoll_fd;
    coroutine_t coro;

    struct list_node run_list;
} event_loop_t;

#define WAITIN  EPOLLIN
#define WAITOUT EPOLLOUT
#define WAITERR EPOLLERR
#define WAITHUP EPOLLHUP

#else
#error "No poller available for platform"
#endif


extern int event_loop_init(event_loop_t* loop);
extern uint32_t event_loop_wait(event_loop_t* loop, int fd, uint32_t events);
extern void event_loop_schedule(event_loop_t* loop, coroutine_t* coro);


#endif
