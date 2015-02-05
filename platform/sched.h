#ifndef _PLATFORM_SCHED_H
#define _PLATFORM_SCHED_H


#ifdef SCHED_LINUX
#include <signal.h>
#include <time.h>
#endif


struct platform_sched {
#ifdef SCHED_LINUX
    timer_t timer;
#endif
};


/*
 * Initialise platform specific scheduling structure.
 */
extern int platform_sched_init(struct platform_sched* platform_sched);


/*
 * Get the online CPU count.
 */
extern unsigned int platform_sched_cpu_count();


#endif
