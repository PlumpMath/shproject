#include <coro.h>
#include <sched.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>


// 8KB stacks
#define STACK_SIZE 16384


__thread coroutine_t* current_coro;


static void trampoline() {
    current_coro->start(current_coro->value);
    __sched_die();
}


void coroutine_create(coroutine_t* coro, void *(*start)(void*)) {
    void* stack = malloc(STACK_SIZE);
    assert(stack != NULL);

    int result = getcontext(&coro->context);
    assert(result == 0);

    coro->context.uc_stack.ss_sp = stack;
    coro->context.uc_stack.ss_size = STACK_SIZE;
    coro->context.uc_stack.ss_flags = 0;
    makecontext(&coro->context, trampoline, 0);

    coro->start = start;
}


void* coroutine_switch(coroutine_t* coro, void* value) {
    coro->value = value;
    coroutine_t* self = coroutine_self();
    current_coro = coro;

    swapcontext(&self->context, &coro->context);

    return self->value;
}


coroutine_t* coroutine_self() {
    if (current_coro != NULL) {
        return current_coro;
    }

    current_coro = (coroutine_t*)malloc(sizeof(coroutine_t));
/*
    // TODO: is this necessary?
    int result = getcontext(&current_coro->context);
    assert(result == 0);
*/
    return current_coro;
}
