#ifndef _CORO_H
#define _CORO_H

#include <util/list.h>

#include <ucontext.h>


struct context {
    ucontext_t context;
};


/*
 * Initialise a new coroutine in `coro`. The function `start` will
 * be executed when the coroutine is switched to.
 */
extern void context_create(struct context* context, void (*start)(void));


/*
 * Switch to the given coroutine. The function will return when the calling
 * coroutine is switched back to.
 */
extern void context_switch(struct context* from, struct context* to);


/*
 * Get an empty context into which we can store a coroutine state at a later
 * time.
 */
extern void context_empty(struct context* context);


#endif
