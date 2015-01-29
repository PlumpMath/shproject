#include <context.h>
#include <sched.h>

#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>


// 8KB stacks
#define STACK_SIZE 16384


void context_create(struct context* context, void (*start)(void)) {
    void* stack = malloc(STACK_SIZE);
    assert(stack != NULL);

    int result = getcontext(&context->context);
    assert(result == 0);

    context->context.uc_stack.ss_sp = stack;
    context->context.uc_stack.ss_size = STACK_SIZE;
    context->context.uc_stack.ss_flags = 0;
    makecontext(&context->context, (void (*)())start, 0);
}


void context_empty(struct context* context) {
    int result = getcontext(&context->context);
    assert(result == 0);
}


void context_switch(struct context* from, struct context* to) {
    swapcontext(&from->context, &to->context);
}
