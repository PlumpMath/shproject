#ifndef _CORO_H
#define _CORO_H

#include <util/list.h>

#include <ucontext.h>


typedef struct {
    ucontext_t context;
    void* value;
    void* (*start)(void*);
    struct list_node list;
} coroutine_t;


/*
 * Initialise a new coroutine in `coro`. The function `start` will
 * be executed when the coroutine is switched to.
 */
extern void coroutine_create(coroutine_t* coro, void* (*start)(void*), void* arg);

/*
 * Switch to coroutine `coro`. The `value` will be passed either to
 * the coroutines start routine, or returned from coroutine_switch
 * if the coroutine has switched out.
 *
 * The function will return when the calling coroutine is switched
 * back to, and the value passed to that call of coroutine_switch
 * will be returned.
 */
extern void* coroutine_switch(coroutine_t* coro, void* value);

/*
 * Returns a pointer to the current coroutine. If the current thread
 * of execution does not correspond to a coroutine, a new coroutine
 * representing it is returned.
 */
extern coroutine_t* coroutine_self();


#endif
