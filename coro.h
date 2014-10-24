#ifndef _CORO_H
#define _CORO_H

#include <util/list.h>

#include <ucontext.h>


struct coroutine {
    ucontext_t context;
    void* value;
    void* (*start)(void*);
    struct list_node list;
};


/*
 * Initialise a new coroutine in `coro`. The function `start` will
 * be executed when the coroutine is switched to.
 */
extern void coroutine_create(struct coroutine* coro, void* (*start)(void*), void* arg);


/*
 * Switch to the given coroutine. The function will return when the calling
 * coroutine is switched back to.
 */
extern void coroutine_switch(struct coroutine* coro);


/*
 * Returns a pointer to the current coroutine. If the current thread
 * of execution does not correspond to a coroutine, a new coroutine
 * representing it is returned.
 */
extern struct coroutine* coroutine_self();


#endif
