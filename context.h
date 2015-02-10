#ifndef _CONTEXT_H
#define _CONTEXT_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <sys/mman.h>


struct context {
#if defined(CONTEXT_GCC_AMD64)
    long regs[8];
#elif defined(CONTEXT_GCC_X86)
    long regs[6];
#else
#   error Missing context definition for architecture!
#endif
    void* stack;
    size_t stack_size;
};


extern void _context_create(struct context*, void (*fn)(void*), void* stack_top);


/*
 * Initialise a new coroutine in `coro`. The function `start` will
 * be executed when the coroutine is switched to.
 */
static inline void
context_create(struct context* context, void (*fn)(void*), size_t stack_size) {
    void* stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(stack != MAP_FAILED);

    context->stack = stack;
    context->stack_size = stack_size;
    _context_create(context, fn, (void*)((uintptr_t)stack + stack_size));
}


/*
 * Save the current execution state into the given context.
 * Returns false when first called. Returns true when the context is later
 * restored with context_restore.
 */
extern int context_save(struct context*);


/*
 * Restores a context previously saved with a call to context_save. Control
 * flow will be transfered to the original call to context_save, and that
 * function will return true.
 */
extern void context_restore(struct context*) __attribute__((noreturn));


/*
 * Atomically tests that the value at address is equal to the old value, and
 * if so sets it to new value.
 *
 * If the test and set succeeds, the context is restored and the function does
 * not return.
 * Otherwise, the value at address is unchanged, and the value is returned.
 */
extern int context_cmpxchg_restore(struct context* context, int* address,
    int old, int new);


/*
 * Switch to the given coroutine. The function will return when the calling
 * coroutine is switched back to.
 */
static inline void context_switch(struct context* from, struct context* to) {
    int switched = context_save(from);
    if (switched == 0) {
        context_restore(to);
    }
}


/*
 * Get an empty context into which we can store a coroutine state at a later
 * time.
 */
static inline void context_empty(struct context* context) {
}


/*
 * Destroy a context, freeing all resources associated with it.
 */
static inline void context_free(struct context* context) {
    int result = munmap(context->stack, context->stack_size);
    assert(result == 0);
}


#endif
