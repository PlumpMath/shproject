#ifndef _PLATFORM_SCHED_H
#define _PLATFORM_SCHED_H


#ifdef SCHED_POSIX
#include <signal.h>
#include <time.h>
#endif


typedef struct platform_sched {
#ifdef SCHED_POSIX
    timer_t timer;
#endif
} platform_sched_t;


/*
 * Initialise platform specific scheduling structure.
 */
extern int platform_sched_init(platform_sched_t* platform_sched);


/*
 * Prevent rescheduling the current coroutine. Calls to this cannot be
 * nested.
 */
extern void platform_sched_block(platform_sched_t* platform_sched);


/*
 * Re-enable scheduling (after a call to platform_sched_block).
 */
extern void platform_sched_unblock(platform_sched_t* platform_sched);


#endif
