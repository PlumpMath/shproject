#include <coro.h>
#include <platform/sched.h>
#include <util/list.h>

#include <stdio.h>
#include <stdlib.h>


typedef struct scheduler {
    platform_sched_t platform_sched;

    struct list_node run_list;
    struct list_node inactive_list;
} scheduler_t;


__thread scheduler_t* scheduler;


/*
 * Internal call to initialise the scheduler.
 */
static inline void __sched_init() {
    if (scheduler != NULL) {
        return;
    }

    scheduler_t* sched = (scheduler_t*)malloc(sizeof(scheduler_t));

    list_init(&sched->run_list);
    list_init(&sched->inactive_list);

    // Platform initialisation may trigger code that will use scheduler,
    // so set it now.
    scheduler = sched;

    int result = platform_sched_init(&sched->platform_sched);
    assert(result == 0);
}


/*
 * Internal call to reschedule another coroutine. The context that should be
 * saved for the current coroutine is passed in.
 */
void __sched_reschedule_current() {
    if (list_empty(&scheduler->run_list)) {
        return;
    }

    struct list_node* node = list_pop_front(&scheduler->run_list);
    assert(node != NULL);

    // Put the current coroutine back on the run list
    coroutine_t* self = coroutine_self();

    assert(!list_in_list(&self->list));
    list_push_back(&scheduler->run_list, &self->list);

    coroutine_t* coro = LIST_ITEM(node, coroutine_t, list);
    coroutine_switch(coro, NULL);
}


void __sched_die() {
    if (list_empty(&scheduler->run_list)) {
        fprintf(stderr, "DEADLOCK\n");
        exit(EXIT_FAILURE);
    }

    struct list_node* node = list_pop_front(&scheduler->run_list);
    assert(node != NULL);

    coroutine_t* coro = LIST_ITEM(node, coroutine_t, list);
    coroutine_switch(coro, NULL);
}


/*
 * Schedule the given coroutine to run at some point in the future.
 */
void sched_schedule(coroutine_t* coro) {
    __sched_init();

    platform_sched_block(&scheduler->platform_sched);
    list_push_back(&scheduler->run_list, &coro->list);
    platform_sched_unblock(&scheduler->platform_sched);
}


/*
 * Yield the caller's timeslice to the next runnable coroutine (if there
 * is one).
 */
void sched_yield() {
    __sched_init();

    platform_sched_block(&scheduler->platform_sched);
    __sched_reschedule_current();
    platform_sched_unblock(&scheduler->platform_sched);
}
