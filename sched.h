#ifndef SCHED_H
#define SCHED_H

#include <coro.h>


// Internal
extern void __sched_reschedule_current();
extern void __sched_init();
extern void __sched_die();


extern void sched_schedule(coroutine_t* coro);
extern void sched_yield();

#endif
