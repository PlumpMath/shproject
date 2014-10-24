#ifndef _SCHED_H
#define _SCHED_H

#include <coro.h>

#include <stdint.h>


// Forward declaration
struct timespec;


/*
 * Enqueue the given coroutine on the run queue. It will run at some point
 * in the future.
 */
extern void sched_schedule(coroutine_t* coro);


/*
 * Suspend the calling coroutine until the given absolute timeout expires.
 */
extern void sched_wait(const struct timespec* timeout);


/*
 * Suspend the calling coroutine until it is explicitly rescheduled.
 */
extern void sched_suspend();


/*
 * Suspend the calling coroutine until an event occurs on a file descriptor.
 */
extern uint32_t sched_event_wait(int fd, uint32_t events);


/*
 * Yield the caller's timeslice to the next runnable coroutine (if there
 * is one).
 */
extern void sched_yield();


/*
 * Called by the platform poller when an event occurs on a file descriptor
 * registered with it.
 */
extern void sched_poll_event(void* key, uint32_t revents);


/*
 * Called by the platform scheduler to schedule the next coroutine.
 */
extern void sched_reschedule();


#endif
