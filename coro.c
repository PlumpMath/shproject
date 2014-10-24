#include <coro.h>
#include <sched.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>


// 8KB stacks
#define STACK_SIZE 16384


__thread struct coroutine* current_coro;


static void trampoline() {
    current_coro->start(current_coro->value);
    sched_suspend();  // TODO: leaks coroutine
}


void coroutine_create(struct coroutine* coro, void *(*start)(void*), void* arg) {
    void* stack = malloc(STACK_SIZE);
    assert(stack != NULL);

    int result = getcontext(&coro->context);
    assert(result == 0);

    coro->context.uc_stack.ss_sp = stack;
    coro->context.uc_stack.ss_size = STACK_SIZE;
    coro->context.uc_stack.ss_flags = 0;
    makecontext(&coro->context, trampoline, 0);

    coro->start = start;
    coro->value = arg;

    list_node_init(&coro->list);
}


void coroutine_switch(struct coroutine* coro) {
    struct coroutine* self = coroutine_self();
    current_coro = coro;

    swapcontext(&self->context, &coro->context);
}


struct coroutine* coroutine_self() {
    if (current_coro != NULL) {
        return current_coro;
    }

    current_coro = (struct coroutine*)malloc(sizeof(struct coroutine));
    return current_coro;
}
