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


extern void coroutine_create(coroutine_t* coro, void* (*start)(void*));

extern void* coroutine_switch(coroutine_t* coro, void* value);

extern coroutine_t* coroutine_self();


#endif
