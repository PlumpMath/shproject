#include <context.h>
#include <scheduler.h>
#include <platform/lock.h>
#include <platform/poll.h>
#include <platform/sched.h>
#include <util/heap.h>
#include <util/list.h>

#include <errno.h>
#include <limits.h>
#include <sched.h>
#include <pthread.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>


static const size_t DEFAULT_STACK = 4 * 4096;


#define glock()     mutex_lock(&gsched->lock)
#define gunlock()   mutex_unlock(&gsched->lock)
#define slock()     mutex_lock(&lsched->lock)
#define sunlock()   mutex_unlock(&lsched->lock)


#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))


#define STATE_RUNNABLE  1
#define STATE_BLOCKING  2
#define STATE_BLOCKED   4


struct coroutine {
    int state;
    struct context context;
    struct list_node list;

    int private_errno;

    void* (*start)(void*);
    void* arg;
};


struct local_sched {
    struct list_node run_list;
    struct list_node inactive_list;

    struct coroutine* current_coro;
    struct coroutine* event_loop_coro;

    unsigned int preempt_lock;
    struct mutex lock;

    struct platform_sched platform_sched;
    bool platform_sched_initialised;

    char death_stack[4096];
};


struct global_sched {
    struct poller poller;

    struct heap timer_heap;

    struct mutex lock;

    unsigned int local_size;
    unsigned int local_count;

    struct local_sched* local_schedulers[];
} global_sched;


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
 * Global scheduler instance.
 */
static struct global_sched* gsched = NULL;

/*
 * Local per-thread scheduler instance.
 */
static __thread struct local_sched* lsched = NULL;


static struct coroutine* sched_make_coro(void* (*start)(void*), void* arg);
static void* sched_loop(void* arg);
static void sched_handle_timers(struct global_sched* sched);
static void sched_switch_context(struct local_sched* sched, struct coroutine* coro);

static struct coroutine* sched_dequeue_locked(struct local_sched* sched);
static void sched_enqueue_locked(struct local_sched* sched, struct coroutine* coro);

static struct coroutine* sched_get_current(struct local_sched* sched);
static void sched_prepare_to_suspend();


static inline void sched_block_preempt(struct local_sched* sched) {
    assert(!sched->preempt_lock);
    sched->preempt_lock = 1;
    __sync_synchronize();
}

static inline void sched_unblock_preempt(struct local_sched* sched) {
    assert(sched->preempt_lock);
    sched->preempt_lock = 0;
    __sync_synchronize();
}

static inline int sched_can_preempt(struct local_sched* sched) {
    return sched->preempt_lock == 0;
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
static long sched_poll_timeout(struct global_sched* global, struct local_sched* local) {
    if (!list_empty(&local->run_list)) {
        return 0;
    }

    struct timer_wait* timer = (struct timer_wait*)heap_min(&global->timer_heap);
    if (timer == NULL) {
        return -1;
    }

    long expiration = timer_expiration(timer);
    return expiration < 0 ? 0 : expiration;
}


static void sched_local_start_thread(struct local_sched* local) {
    pthread_attr_t thread_attr;
    int result = pthread_attr_init(&thread_attr);
    assert(result == 0);

    size_t stack_size = MAX(DEFAULT_STACK, PTHREAD_STACK_MIN);
    result = pthread_attr_setstacksize(&thread_attr, stack_size);
    assert(result == 0);

    // Our new coroutine is already enqueued on the local's runqueue. We will
    // reuse our new pthread's stack for the event loop coroutine.
    // The dummy coroutine has already been initialized in sched_new_local().
    local->event_loop_coro = local->current_coro;

    pthread_t thread;
    result = pthread_create(&thread, &thread_attr, sched_loop, (void*)local);
    assert(result == 0);
}


static struct global_sched* sched_new_global(unsigned int thread_count) {
    struct global_sched* global = (struct global_sched*)malloc(
        sizeof(struct global_sched) +
        thread_count * sizeof(struct local_sched*)
    );

    int result = heap_init(&global->timer_heap, timer_compare);
    assert(result == 0);

    for (int i = 0; i < thread_count; i++) {
        global->local_schedulers[i] = NULL;
    }

    global->local_size = thread_count;
    global->local_count = 0;

    mutex_init(&global->lock);

    result = platform_poll_init(&global->poller);
    assert(result == 0);

    return global;
}


static struct local_sched* sched_new_local() {
    struct local_sched* local = (struct local_sched*)malloc(
        sizeof(struct local_sched)
    );
    assert(local != NULL);

    mutex_init(&local->lock);

    local->preempt_lock = 0;

    list_init(&local->run_list);
    list_init(&local->inactive_list);

    // Initialise the coroutine that will correspond to the local's thread
    // (either the new pthread we create or the main thread).
    struct coroutine* coro = (struct coroutine*)malloc(sizeof(struct coroutine));
    assert(coro != NULL);

    context_empty(&coro->context);
    list_node_init(&coro->list);
    local->current_coro = coro;

    local->platform_sched_initialised = false;
    return local;
}


/*
 * Internal call to initialise the scheduler.
 */
static void sched_init() {
    if (lsched != NULL) {
        return;
    }

    unsigned int cpu_count = platform_sched_cpu_count();
    struct global_sched* global = sched_new_global(cpu_count);
    assert(global != NULL);

    // Initialise the local scheduler for this thread.
    struct local_sched* local = sched_new_local();
    global->local_schedulers[0] = local;
    global->local_count++;

    // Platform initialisation may trigger code that will use scheduler,
    // so set it now.
    gsched = global;
    lsched = local;

    // We can't use sched_new_coroutine here as we don't want to trigger
    // the lazy local initialization.
    local->event_loop_coro = sched_make_coro(sched_loop, local);
    assert(local->event_loop_coro != NULL);

    sched_enqueue_locked(local, local->event_loop_coro);

    int result = platform_sched_init(&local->platform_sched);
    assert(result == 0);
    local->platform_sched_initialised = true;
}


static void* sched_loop(void* arg) {
    struct local_sched* sched = (struct local_sched*)arg;
    lsched = sched;  // Set the local scheduler for this thread in TLS.

    if (!sched->platform_sched_initialised) {
        int result = platform_sched_init(&sched->platform_sched);
        assert(result == 0);
    }

    for (;;) {
        long timeout = sched_poll_timeout(gsched, sched);
        int events = platform_poll_poll(&gsched->poller, timeout);
        if (events == -1) {
            perror("event poll");
            exit(EXIT_FAILURE);
        }

        sched_handle_timers(gsched);
        if (timeout == 0) {
            sched_resched();
        }
    }
}


/*
 * Handle expired timers, scheduling coroutines that are now ready to run.
 */
static void sched_handle_timers(struct global_sched* global) {
    for (;;) {
        // We can check the timer heap fairly safely without the lock.
        if (heap_empty(&global->timer_heap)) {
            return;
        }

        // Now take the lock
        glock();

        struct timer_wait* timer = (struct timer_wait*)heap_min(
            &global->timer_heap);
        if (timer == NULL) {
            gunlock();
            return;
        }

        if (timer_expiration(timer) <= 0) {
            heap_pop_min(&global->timer_heap);
            sched_schedule(timer->coro);
        } else {
            gunlock();
            return;
        }

        gunlock();
    }
}


/*
 * Schedule another coroutine in place of this one.
 */
void sched_resched_callback() {
    if (!sched_can_preempt(lsched)) {
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

    slock();
    if (list_empty(&lsched->run_list)) {
        sunlock();
        return;
    }
    struct coroutine* current = sched_get_current(lsched);
    sched_enqueue_locked(lsched, current);
    struct coroutine* coro = sched_dequeue_locked(lsched);

    sunlock();

    sched_switch_context(lsched, coro);
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
        .coro = sched_get_current(lsched)
    };

    sched_prepare_to_suspend();

    int result = platform_poll_register(&gsched->poller, fd, events, (void*)&wait);
    assert(result == 0);

    sched_suspend();

    // TODO: potential race between unregistering the fd and another scheduler
    // receiving an event for the fd and trying to reschedule it. Won't manifest
    // with epoll because we use EPOLLONESHOT.
    result = platform_poll_unregister(&gsched->poller, fd);
    assert(result == 0);

    assert(wait.events != 0);
    return wait.events;
}


/*
 * Called by the platform poller when an event occurs on a file descriptor
 * registered with it.
 */
void sched_poll_event(void* key, uint32_t revents) {
    struct event_wait* wait = (struct event_wait*)key;
    struct coroutine* coro = __atomic_exchange_n(&wait->coro, NULL, __ATOMIC_ACQ_REL);
    if (coro != NULL) {
        wait->events = revents;
        sched_schedule(coro);
    }
}


/*
 * Suspend the calling coroutine until the given (absolute) time.
 */
void sched_wait(const struct timespec* time) {
    sched_init();

    struct timer_wait wait = {
        .time = time,
        .coro = sched_get_current(lsched)
    };

    sched_prepare_to_suspend();

    glock();
    int result = heap_push(&gsched->timer_heap, (void*)&wait);
    gunlock();

    assert(result == 0);

    sched_suspend();
}


static void sched_prepare_to_suspend() {
    struct coroutine* coro = sched_get_current(lsched);
    __atomic_store_n(&coro->state, STATE_BLOCKING, __ATOMIC_RELEASE);
}


/*
 * Schedule another coroutine; don't reschedule this one.
 */
void sched_suspend() {
    sched_init();

    struct coroutine* current = sched_get_current(lsched);

    bool switched_back = context_save(&current->context);
    if (!switched_back) {
        slock();
        struct coroutine* coro = sched_dequeue_locked(lsched);
        sunlock();

        sched_block_preempt(lsched);

        current->private_errno = errno;
        lsched->current_coro = coro;
        errno = coro->private_errno;

        // We need to set our state to BLOCKED and sleep. After setting
        // BLOCKED, another scheduler can schedule the coroutine, reusing
        // this stack space.
        // Therefore we use a special assembly function to do the context
        // switch that doesn't use the stack.
        context_cmpxchg_restore(&coro->context, &current->state, STATE_BLOCKING,
            STATE_BLOCKED);

        // If we get here -- it means another scheduler awoke the coroutine
        // right before it got to sleep. We need to reschedule the one we were
        // about to switch to, and correct the current coroutine pointer.
        lsched->current_coro = current;
        errno = current->private_errno;

        slock();
        sched_enqueue_locked(lsched, coro);
        sunlock();
    }

    sched_unblock_preempt(lsched);
}


/*
 * Schedule the given coroutine to run at some point in the future.
 */
void sched_schedule(struct coroutine* coro) {
    sched_init();

    int previous_state = __atomic_exchange_n(&coro->state, STATE_RUNNABLE,
        __ATOMIC_ACQ_REL);

    if (previous_state != STATE_BLOCKED) {
        // We might have raced with another waker, or the even the coroutine
        // going to sleep. In this case we don't need to do anything -- the
        // sleeping coroutine will catch our wake up when it tries to xchg
        // STATE_BLOCKED.
        return;
    }

    slock();
    sched_enqueue_locked(lsched, coro);
    sunlock();
}


/*
 * Dequeue the next coroutine to run. Scheduler must be locked!
 */
static struct coroutine* sched_dequeue_locked(struct local_sched* local) {
    struct list_node* node = list_pop_front(&local->run_list);
    assert(node != NULL);
    return LIST_ITEM(node, struct coroutine, list);
}


/*
 * Enqueue's the given coroutine on the runqueue. Scheduler must be locked!
 */
static void sched_enqueue_locked(struct local_sched* local, struct coroutine* coro) {
    assert(!list_in_list(&coro->list));
    list_push_back(&local->run_list, &coro->list);
}


/*
 * Get the current coroutine. Only call with preemption blocked!
 */
static struct coroutine* sched_get_current(struct local_sched* local) {
    return local->current_coro;
}


/*
 * Switch context to the given coroutine. Does not requeue the current
 * coroutine. Only call with preemption blocked!
 */
static void sched_switch_context(struct local_sched* local, struct coroutine* coro) {
    sched_block_preempt(local);
    struct coroutine* previous = sched_get_current(local);
    assert(previous != NULL);

    previous->private_errno = errno;

    local->current_coro = coro;
    errno = coro->private_errno;
    context_switch(&previous->context, &coro->context);

    // We've been switched back to - preemption is blocked before a switch
    // so we must unblock it again. However, we might be being switched back
    // to on a different thread than we started off on - so we need to refer
    // to our TLS variable, and not the parameter passed in.
    sched_unblock_preempt(lsched);
}


/*
 * Switch to the given coroutine, without requeueing the current one.
 */
void sched_suspend_switch(struct coroutine* coro) {
    sched_init();

    sched_switch_context(lsched, coro);
}


/*
 * Give up the caller's timeslice to the given coroutine, placing the calling
 * coroutine back on the runqueue.
 */
void sched_resched_switch(struct coroutine* coro) {
    sched_init();

    struct coroutine* current = sched_get_current(lsched);

    slock();
    sched_enqueue_locked(lsched, current);
    sunlock();

    sched_switch_context(lsched, coro);
}


static void free_coro() {
    struct coroutine* coro = lsched->current_coro;

    // Sanity check. We shouldn't be registered for any poll events or timers
    // either - but we can't check that easily.
    assert(!list_in_list(&coro->list));

    // Preemption is blocked so this is safe.
    context_free(&coro->context);
    free(coro);

    // Get our next coroutine.
    slock();
    struct coroutine* next = sched_dequeue_locked(lsched);
    sunlock();

    lsched->current_coro = next;
    // The restored context will unblock preemption.
    context_restore(&next->context);
}


static void sched_coro_die() {
    // Block preemption for duration of the process, until we switch to
    // the next coroutine.
    sched_block_preempt(lsched);

    struct context context;
    _context_create(&context, free_coro,
        &lsched->death_stack[0] + sizeof(lsched->death_stack));
    context_restore(&context);
}

/*
 * Workaround lack of (pointer) argument passing in some context-switching
 * functions (i.e. ucontext).
 */
static void coro_trampoline() {
    struct coroutine* coro = lsched->current_coro;
    sched_unblock_preempt(lsched);
    coro->start(coro->arg);

    sched_coro_die();
}


static struct coroutine* sched_make_coro(void* (*start)(void*), void* arg) {
    struct coroutine* coro = (struct coroutine*)malloc(sizeof(struct coroutine));
    assert(coro != NULL);

    list_node_init(&coro->list);

    context_create(&coro->context, coro_trampoline, DEFAULT_STACK);
    coro->state = STATE_BLOCKED;
    coro->start = start;
    coro->arg = arg;
    return coro;
}

/*
 * Create and schedule a new coroutine which will execute function 'start',
 * passing 'arg' as the first argument.
 */
struct coroutine* sched_new_coroutine(void* (*start)(void*), void* arg) {
    sched_init();
    struct coroutine* coro = sched_make_coro(start, arg);

    if (__atomic_load_n(&gsched->local_count, __ATOMIC_ACQUIRE) >= gsched->local_size) {
fast_path:
        sched_schedule(coro);
        return coro;
    }

    unsigned int slot = __atomic_fetch_add(&gsched->local_count, 1, __ATOMIC_RELEASE);
    if (slot >= gsched->local_size) {
        goto fast_path;
    }

    // Can do this without a lock - the slot we picked is exclusive.
    struct local_sched* local = sched_new_local();
    gsched->local_schedulers[slot] = local;

    // We don't need to lock the local either.
    coro->state = STATE_RUNNABLE;
    sched_enqueue_locked(local, coro);
    sched_local_start_thread(local);
    return coro;
}
