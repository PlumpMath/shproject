#ifndef _CONTEXT_H
#define _CONTEXT_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>


struct context {
#ifdef CONTEXT_GCC_AMD64
    long regs[8];
#else
#   error Missing context definition for architecture!
#endif
};


extern void _context_create(struct context*, void (*fn)(void*), void* stack_top);
extern int _context_save(struct context*);
extern void _context_restore(struct context*);


/*
 * Initialise a new coroutine in `coro`. The function `start` will
 * be executed when the coroutine is switched to.
 */
static inline void
context_create(struct context* context, void (*fn)(void*), size_t stack_size) {
    void* stack = malloc(stack_size);
    assert(stack != NULL);
    _context_create(context, fn, (void*)((uintptr_t)stack + stack_size));
}


/*
 * Switch to the given coroutine. The function will return when the calling
 * coroutine is switched back to.
 */
static inline void
context_switch(struct context* from, struct context* to) {
    int switched = _context_save(from);
    if (switched == 0) {
        _context_restore(to);
    }
}


/*
 * Get an empty context into which we can store a coroutine state at a later
 * time.
 */
static inline void context_empty(struct context* context) {
}


/*
 * Save the current execution state into the given context.
 * Returns false when first called. Returns true when the context is later
 * restored with context_restore.
 */
static inline bool context_save(struct context* context) {
    return _context_save(context) != 0;
}


/*
 * Restores a context previously saved with a call to context_save. Control
 * flow will be transfered to the original call to context_save, and that
 * function will return true.
 */
static inline void context_restore(struct context* context) __attribute__ ((noreturn));

static inline void context_restore(struct context* context) {
    _context_restore(context);
}


#endif
