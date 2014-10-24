#include <coro.h>
#include <platform/poll.h>
#include <platform/sched.h>
#include <sched.h>
#include <util/heap.h>
#include <util/list.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>


struct scheduler {
    struct platform_sched platform_sched;
    struct poller poller;

    struct list_node run_list;
    struct list_node inactive_list;

    struct heap timer_heap;

    struct coroutine event_loop_coro;
    struct coroutine* current_coro;
    int lock;
};


/*
 * Records the file descriptor we are waiting on and the coroutine to switch
 * back to when we get any events
 */
struct event_wait {
    int fd;
    int events;
    struct coroutine* coro;
};


/*
 * Records the timer timeout and the coroutine to switch to when it expires.
 */
struct timer_wait {
    const struct timespec* time;
    struct coroutine* coro;
};


/*
 * Per-thread scheduler instance.
 */
__thread struct scheduler* scheduler;


static void* sched_loop(void* arg);
static void sched_handle_timers(struct scheduler* scheduler);


static inline void sched_block_preempt(struct scheduler* sched) {
    assert(!sched->lock);
    sched->lock = 1;
    __sync_synchronize();
}


static inline void sched_unblock_preempt(struct scheduler* sched) {
    assert(sched->lock);
    sched->lock = 0;
    __sync_synchronize();
}


static inline int sched_can_preempt(struct scheduler* sched) {
    return sched->lock == 0;
}


/*
 * Timer comparator used for the timer heap.
 */
static int timer_compare(void* timer1, void* timer2) {
    struct timer_wait* t1 = (struct timer_wait*)timer1;
    struct timer_wait* t2 = (struct timer_wait*)timer2;

    long seconds = t1->time->tv_sec - t2->time->tv_sec;
    if (seconds != 0) {
        return seconds;
    }
    return t1->time->tv_nsec - t2->time->tv_nsec;
}


/*
 * Calculate relative expiration of the timer in milliseconds.
 */
static long timer_expiration(struct timer_wait* timer) {
    struct timespec now;
    int result = clock_gettime(CLOCK_REALTIME, &now);
    assert(result == 0);

    long millis = (timer->time->tv_sec - now.tv_sec) * 1000L;
    return millis + (timer->time->tv_nsec - now.tv_nsec) / 1000000L;
}


/*
 * Calculate the timeout that should be used for the poll call. If there are
 * coroutines on the runlist, the call should not block at all. Otherwise
 * this is the expiration time of the soonest-expiring timer.
 */
static long sched_poll_timeout(struct scheduler* sched) {
    if (!list_empty(&sched->run_list)) {
        return 0;
    }

    struct timer_wait* timer = (struct timer_wait*)heap_min(&sched->timer_heap);
    if (timer == NULL) {
        return -1;
    }

    long expiration = timer_expiration(timer);
    return expiration < 0 ? 0 : expiration;
}


/*
 * Internal call to initialise the scheduler.
 */
static void sched_init() {
    if (scheduler != NULL) {
        return;
    }

    struct scheduler* sched = (struct scheduler*)malloc(sizeof(struct scheduler));

    // Initialise lists
    list_init(&sched->run_list);
    list_init(&sched->inactive_list);

    int result = heap_init(&sched->timer_heap, timer_compare);
    assert(result == 0);

    // Platform initialisation may trigger code that will use scheduler,
    // so set it now.
    scheduler = sched;

    result = platform_poll_init(&sched->poller);
    assert(result == 0);

    coroutine_create(&sched->event_loop_coro, (void* (*)(void*))sched_loop, sched);
    list_push_back(&sched->run_list, &sched->event_loop_coro.list);

    scheduler = sched;

    result = platform_sched_init(&sched->platform_sched);
    assert(result == 0);
}


static void* sched_loop(void* arg) {
    struct scheduler* sched = (struct scheduler*)arg;

    for (;;) {
        long timeout = sched_poll_timeout(sched);
        int events = platform_poll_poll(&sched->poller, timeout);

        if (events == -1) {
            perror("event poll");
            exit(EXIT_FAILURE);
        }

        sched_handle_timers(sched);
    }
}


/*
 * Handle expired timers, scheduling coroutines that are now ready to run.
 */
static void sched_handle_timers(struct scheduler* sched) {
    for (;;) {
        struct timer_wait* timer = (struct timer_wait*)heap_min(
            &sched->timer_heap);
        if (timer == NULL) {
            return;
        }

        if (timer_expiration(timer) <= 0) {
            heap_pop_min(&sched->timer_heap);
            sched_schedule(timer->coro);
        } else {
            return;
        }
    }
}


/*
 * Schedule another coroutine in place of this one.
 */
void sched_reschedule() {
    if (list_empty(&scheduler->run_list) || !sched_can_preempt(scheduler)) {
        return;
    }

    struct list_node* node = list_pop_front(&scheduler->run_list);
    assert(node != NULL);

    // Put the current coroutine back on the run list
    struct coroutine* self = coroutine_self();

    assert(!list_in_list(&self->list));
    list_push_back(&scheduler->run_list, &self->list);

    struct coroutine* coro = LIST_ITEM(node, struct coroutine, list);
    coroutine_switch(coro);
}


/*
 * Suspend the calling coroutine until an event occurs on the given file
 * descriptor.
 */
uint32_t sched_event_wait(int fd, uint32_t events) {
    sched_init();

    struct event_wait wait = {
        .fd = fd,
        .events = 0,
        .coro = coroutine_self()
    };
    platform_poll_register(&scheduler->poller, fd, events, (void*)&wait);

    // TODO: prevent missed wakeup when multiple processors involved
    sched_suspend();

    platform_poll_unregister(&scheduler->poller, fd);

    assert(wait.events != 0);
    return wait.events;
}


/*
 * Called by the platform poller when an event occurs on a file descriptor
 * registered with it.
 */
void sched_poll_event(void* key, uint32_t revents) {
    struct event_wait* wait = (struct event_wait*)key;
    wait->events = revents;

    sched_schedule(wait->coro);
}


/*
 * Suspend the calling coroutine until the given (absolute) time.
 */
void sched_wait(const struct timespec* time) {
    sched_init();

    struct timer_wait wait = {
        .time = time,
        .coro = coroutine_self()
    };

    int result = heap_push(&scheduler->timer_heap, (void*)&wait);
    assert(result == 0);

    sched_suspend();
}


/*
 * Schedule another coroutine; don't reschedule this one.
 */
void sched_suspend() {
    assert(!list_empty(&scheduler->run_list));

    struct list_node* node = list_pop_front(&scheduler->run_list);
    assert(node != NULL);
    struct coroutine* coro = LIST_ITEM(node, struct coroutine, list);
    coroutine_switch(coro);
}


/*
 * Schedule the given coroutine to run at some point in the future.
 */
void sched_schedule(struct coroutine* coro) {
    sched_init();

    sched_block_preempt(scheduler);
    assert(!list_in_list(&coro->list));
    list_push_back(&scheduler->run_list, &coro->list);
    sched_unblock_preempt(scheduler);
}


/*
 * Yield the caller's timeslice to the next runnable coroutine (if there
 * is one).
 */
void sched_yield() {
    sched_init();

    sched_block_preempt(scheduler);
    sched_reschedule();
    sched_unblock_preempt(scheduler);
}
