#ifndef _SCHED_H
#define _SCHED_H

#include <stdint.h>


// Forward declarations
struct coroutine;
struct timespec;


/*
 * Create a new coroutine which will execute the given function, passing
 * start as the first argument.
 */
extern struct coroutine* sched_new_coroutine(void* (*start)(void*), void* arg);


/*
 * Enqueue the given coroutine on the run queue. It will run at some point
 * in the future.
 */
extern void sched_schedule(struct coroutine* coro);


/*
 * Suspend the calling coroutine until the given absolute timeout expires.
 */
extern void sched_wait(const struct timespec* timeout);


/*
 * Suspend the calling coroutine until it is explicitly rescheduled.
 */
extern void sched_suspend();


/*
 * Reschedule the current coroutine and run another in its place.
 */
extern void sched_resched();


/*
 * Suspend the calling coroutine until an event occurs on a file descriptor.
 */
extern uint32_t sched_event_wait(int fd, uint32_t events);


/*
 * Called by the platform poller when an event occurs on a file descriptor
 * registered with it.
 */
extern void sched_poll_event(void* key, uint32_t revents);


/*
 * Called by the platform scheduler to schedule the next coroutine.
 */
extern void sched_resched_callback();


#endif
