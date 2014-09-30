#include <coro.h>

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>


// 8KB stacks
#define STACK_SIZE 8192


static pthread_key_t current_key;
static pthread_once_t current_once = PTHREAD_ONCE_INIT;

static void make_key() {
	pthread_key_create(&current_key, NULL);
}


void coroutine_create(coroutine_t* coro, void *(*fn)(void*)) {
	void* stack = malloc(STACK_SIZE);
	assert(stack != NULL);

	int result = getcontext(&coro->context);
	assert(result == 0);

	coro->context.uc_stack.ss_sp = stack;
	coro->context.uc_stack.ss_size = STACK_SIZE;
	coro->context.uc_stack.ss_flags = 0;
	makecontext(&coro->context, (void (*)(void))fn, 0);
}


void* coroutine_switch(coroutine_t* coro, void* value) {
	coro->value = value;
	coroutine_t* self = coroutine_self();

	pthread_setspecific(current_key, coro);
	swapcontext(&self->context, &coro->context);

	return self->value;
}


coroutine_t* coroutine_self() {
	pthread_once(&current_once, make_key);

	coroutine_t* self = pthread_getspecific(current_key);
	if (self != NULL) {
		return self;
	}

	self = (coroutine_t*)malloc(sizeof(coroutine_t));
	assert(self != NULL);

	int result = getcontext(&self->context);
	assert(result == 0);

	pthread_setspecific(current_key, (void*)self);
	return self;
}
