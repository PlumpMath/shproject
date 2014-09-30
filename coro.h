#include <ucontext.h>


typedef struct {
	ucontext_t context;
	void* value;
} coroutine_t;


extern void coroutine_create(coroutine_t* coro, void *(*fn)(void*));

extern void* coroutine_switch(coroutine_t* coro, void* value);

extern coroutine_t* coroutine_self();
