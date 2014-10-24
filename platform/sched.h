#ifndef _PLATFORM_SCHED_H
#define _PLATFORM_SCHED_H


#ifdef SCHED_POSIX
#include <signal.h>
#include <time.h>
#endif


struct platform_sched {
#ifdef SCHED_POSIX
    timer_t timer;
#endif
};


/*
 * Initialise platform specific scheduling structure.
 */
extern int platform_sched_init(struct platform_sched* platform_sched);


/*
 * Prevent rescheduling the current coroutine. Calls to this cannot be
 * nested.
 */
extern void platform_sched_block(struct platform_sched* platform_sched);


/*
 * Re-enable scheduling (after a call to platform_sched_block).
 */
extern void platform_sched_unblock(struct platform_sched* platform_sched);


#endif
