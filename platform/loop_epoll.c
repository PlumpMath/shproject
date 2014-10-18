#include <coro.h>
#include <platform/loop.h>
#include <util/heap.h>
#include <util/list.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>


// TODO: a lot of the things in here aren't epoll/Linux specific and should
// be moved to some commmon file. Almost everything but the loop body itself
// could be moved out.


/*
 * Records the file descriptor we are waiting on and the coroutine to switch
 * back to when we get any events
 */
struct wait_data {
    int fd;
    int events;
    coroutine_t* coro;
};


/*
 * Records the timer timeout and the coroutine to switch to when it expires.
 */
struct timer_data {
    const struct timespec* time;
    coroutine_t* coro;
};


static const int MAX_EVENTS = 20;


/*
 * Pops the next runnable coroutine from the run list. Returns NULL if the
 * list is empty.
 */
static coroutine_t* next_runnable_coro(event_loop_t* loop) {
    if (list_empty(&loop->run_list)) {
        return NULL;
    }
    struct list_node* front = list_pop_front(&loop->run_list);
    return LIST_ITEM(front, coroutine_t, list);
}


/*
 * Calculates relative expiration of `timer` in milliseconds.
 */
static long timer_expiration(struct timer_data* timer) {
    struct timespec now;
    int result = clock_gettime(CLOCK_REALTIME, &now);
    assert(result == 0);

    long millis = (timer->time->tv_sec - now.tv_sec) * 1000L;
    return millis + (timer->time->tv_nsec - now.tv_nsec) / 1000000L;
}


/*
 * Calculates the timeout we should use for the epoll call. This
 * is the relative expiration time of the soonest-expiring timer.
 */
static long get_timeout_millisecs(event_loop_t* loop) {
    struct timer_data* timer = (struct timer_data*)heap_min(&loop->timer_heap);
    if (timer == NULL) {
        return -1;
    }

    long expiration = timer_expiration(timer);
    return expiration < 0 ? 0 : expiration;
}


static void handle_timers(event_loop_t* loop) {
    for (;;) {
        struct timer_data* timer = (struct timer_data*)heap_min(&loop->timer_heap);
        if (timer == NULL) {
            return;
        }

        if (timer_expiration(timer) <= 0) {
            heap_pop_min(&loop->timer_heap);
            coroutine_switch(timer->coro, NULL);
        } else {
            return;
        }
    }
}


static void* _event_loop(event_loop_t* loop) {
    struct epoll_event events[20];

    for (;;) {
        for (;;) {
            coroutine_t* runnable = next_runnable_coro(loop);
            if (runnable == NULL)
                break;
            coroutine_switch(runnable, runnable->value);
        }

        int timeout = (int)get_timeout_millisecs(loop);
        int nfds = epoll_wait(loop->epoll_fd, events, MAX_EVENTS, timeout);

        if (nfds == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        handle_timers(loop);

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


static int timer_compare(void* timer1, void* timer2) {
    struct timer_data* t1 = (struct timer_data*)timer1;
    struct timer_data* t2 = (struct timer_data*)timer2;

    long seconds = t1->time->tv_sec - t2->time->tv_sec;
    if (seconds != 0) {
        return seconds;
    }
    return t1->time->tv_nsec - t2->time->tv_nsec;
}


void event_loop_sleep(event_loop_t* loop, const struct timespec* time) {
    struct timer_data timer = {
        .time = time,
        .coro = coroutine_self()
    };

    int result = heap_push(&loop->timer_heap, (void*)&timer);
    assert(result == 0);

    coroutine_switch(&loop->coro, loop);
}


int event_loop_init(event_loop_t* loop) {
    assert(loop != NULL);

    loop->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (loop->epoll_fd == -1) {
        return errno;
    }

    int result = heap_init(&loop->timer_heap, timer_compare);
    if (result != 0) {
        close(loop->epoll_fd);
        return result;
    }

    coroutine_create(&loop->coro, (void* (*)(void*))_event_loop);
    list_init(&loop->run_list);
    return 0;
}
