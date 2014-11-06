#include <context.h>
#include <platform/poll.h>
#include <platform/sched.h>
#include <sched.h>
#include <util/heap.h>
#include <util/list.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>


struct coroutine {
    struct context context;
    struct list_node list;

    void* (*start)(void*);
    void* arg;
};


struct scheduler {
    struct platform_sched platform_sched;
    struct poller poller;

    struct list_node run_list;
    struct list_node inactive_list;

    struct heap timer_heap;

    struct coroutine* event_loop_coro;
    struct coroutine* current_coro;

    int lock;
    int get_current_count;
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
static void sched_handle_timers(struct scheduler* sched);
static void sched_suspend_locked(struct scheduler* sched);
static void sched_switch_context_locked(struct scheduler* sched, struct coroutine* coro);

static struct coroutine* sched_dequeue_locked(struct scheduler* sched);
static void sched_enqueue_locked(struct scheduler* sched, struct coroutine* coro);

static struct coroutine* sched_get_current_locked(struct scheduler* sched);


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

    sched->lock = 0;
    sched->get_current_count = 0;

    // Platform initialisation may trigger code that will use scheduler,
    // so set it now.
    scheduler = sched;

    result = platform_poll_init(&sched->poller);
    assert(result == 0);

    sched->event_loop_coro = sched_new_coroutine(sched_loop, sched);
    assert(sched->event_loop_coro != NULL);

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
        if (timeout == 0) {
            sched_resched();
        }
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
void sched_resched_callback() {
    if (!sched_can_preempt(scheduler)) {
        return;
    }
    sched_resched();
}


/*
 * Yield the caller's timeslice to the next runnable coroutine (if there
 * is one).
 */
void sched_resched() {
    sched_init();
    sched_block_preempt(scheduler);

    if (list_empty(&scheduler->run_list)) {
        return;
    }

    struct coroutine* current = sched_get_current_locked(scheduler);
    sched_enqueue_locked(scheduler, current);

    struct coroutine* coro = sched_dequeue_locked(scheduler);
    sched_switch_context_locked(scheduler, coro);
}


/*
 * Suspend the calling coroutine until an event occurs on the given file
 * descriptor.
 */
uint32_t sched_event_wait(int fd, uint32_t events) {
    sched_init();
    sched_block_preempt(scheduler);

    struct event_wait wait = {
        .fd = fd,
        .events = 0,
        .coro = sched_get_current_locked(scheduler)
    };
    platform_poll_register(&scheduler->poller, fd, events, (void*)&wait);

    // TODO: prevent missed wakeup when multiple processors involved
    sched_suspend_locked(scheduler);

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
    sched_block_preempt(scheduler);

    struct timer_wait wait = {
        .time = time,
        .coro = sched_get_current_locked(scheduler)
    };

    int result = heap_push(&scheduler->timer_heap, (void*)&wait);
    assert(result == 0);

    sched_suspend_locked(scheduler);
}


static void sched_suspend_locked(struct scheduler* sched) {
    struct coroutine* coro = sched_dequeue_locked(sched);
    sched_switch_context_locked(sched, coro);
}


/*
 * Schedule another coroutine; don't reschedule this one.
 */
void sched_suspend() {
    sched_init();
    sched_block_preempt(scheduler);
    sched_get_current_locked(scheduler);
    sched_suspend_locked(scheduler);
}


/*
 * Schedule the given coroutine to run at some point in the future.
 */
void sched_schedule(struct coroutine* coro) {
    sched_init();
    sched_block_preempt(scheduler);
    sched_enqueue_locked(scheduler, coro);
    sched_unblock_preempt(scheduler);
}


static struct coroutine* sched_dequeue_locked(struct scheduler* sched) {
    struct list_node* node = list_pop_front(&sched->run_list);
    assert(node != NULL);
    return LIST_ITEM(node, struct coroutine, list);
}

static void sched_enqueue_locked(struct scheduler* sched, struct coroutine* coro) {
    assert(!list_in_list(&coro->list));
    list_push_back(&sched->run_list, &coro->list);
}


static struct coroutine* sched_get_current_locked(struct scheduler* sched) {
    if (sched->current_coro == NULL) {
        assert(sched->get_current_count++ == 0);
        sched->current_coro = (struct coroutine*)malloc(sizeof(struct coroutine));
        assert(sched->current_coro != NULL);
        list_node_init(&sched->current_coro->list);
    }

    return sched->current_coro;
}


static void sched_switch_context_locked(struct scheduler* sched, struct coroutine* coro) {
    struct coroutine* previous = sched->current_coro;
    assert(previous != NULL);

    sched->current_coro = coro;
    context_switch(&previous->context, &coro->context);

    sched_unblock_preempt(sched);
}


void sched_suspend_switch(struct scheduler* sched, struct coroutine* coro) {
    sched_init();
    sched_block_preempt(sched);
    sched_get_current_locked(sched);
    sched_switch_context_locked(sched, coro);
}


/*
 * Give up the caller's timeslice to the given coroutine.
 */
void sched_resched_switch(struct coroutine* coro) {
    sched_init();
    sched_block_preempt(scheduler);

    struct coroutine* current = sched_get_current_locked(scheduler);
    sched_enqueue_locked(scheduler, current);
    sched_switch_context_locked(scheduler, coro);
}


static void coro_trampoline() {
    struct coroutine* coro = scheduler->current_coro;
    sched_unblock_preempt(scheduler);
    coro->start(coro->arg);

    sched_suspend();  // TODO: need a mechanism to free coroutine resources
}


struct coroutine* sched_new_coroutine(void* (*start)(void*), void* arg) {
    sched_init();

    struct coroutine* coro = (struct coroutine*)malloc(sizeof(struct coroutine));
    list_node_init(&coro->list);
    assert(coro != NULL);

    context_create(&coro->context, coro_trampoline);
    coro->start = start;
    coro->arg = arg;

    sched_schedule(coro);
    return coro;
}
