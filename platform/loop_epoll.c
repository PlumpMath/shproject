#include <coro.h>
#include <platform/loop.h>
#include <util/list.h>

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>


struct wait_data {
    int fd;
    int events;
    coroutine_t* coro;
};


static const int MAX_EVENTS = 20;
static const int MAX_PRE_POLL = 10;


static coroutine_t* next_runnable_coro(event_loop_t* loop) {
    if (list_empty(&loop->run_list)) {
        return NULL;
    }
    struct list_node* front = list_pop_front(&loop->run_list);
    return LIST_ITEM(front, coroutine_t, list);
}


static void* _event_loop(event_loop_t* loop) {
    struct epoll_event events[20];

    for (;;) {
        for (int i = 0; i < MAX_PRE_POLL; i++) {
            coroutine_t* runnable = next_runnable_coro(loop);
            if (runnable == NULL)
                break;
            coroutine_switch(runnable, runnable->value);
        }

        int nfds = epoll_wait(loop->epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < nfds; i++) {
            struct wait_data* data = (struct wait_data*)events[i].data.ptr;

            epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, data->fd, NULL);
            data->events = events[i].events;
            coroutine_switch(data->coro, data->coro->value);
        }
    }
}


void event_loop_schedule(event_loop_t* loop, coroutine_t* coro) {
    list_push_back(&loop->run_list, &coro->list);
}


uint32_t event_loop_wait(event_loop_t* loop, int fd, uint32_t events) {
    struct wait_data wait = {
        .fd = fd,
        .events = 0,
        .coro = coroutine_self()
    };
    struct epoll_event event = {
        .events = events,
        .data.ptr = &wait
    };
    epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, fd, &event);

    coroutine_switch(&loop->coro, loop);

    assert(wait.events != 0);
    return wait.events;
}


int event_loop_init(event_loop_t* loop) {
    assert(loop != NULL);

    loop->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (loop->epoll_fd == -1) {
        return errno;
    }

    coroutine_create(&loop->coro, (void* (*)(void*))_event_loop);
    list_init(&loop->run_list);
    return 0;
}
