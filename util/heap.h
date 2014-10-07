#ifndef _UTIL_HEAP
#define _UTIL_HEAP

#include <stddef.h>


typedef int (*heap_comparator)(void*, void*);

typedef struct {
    void** heap;
    size_t size;
    size_t items;
    heap_comparator compare;
} heap_t;


extern int heap_init(heap_t* heap, heap_comparator compare);
extern int heap_empty(heap_t* heap);
extern size_t heap_size(heap_t* heap);
extern int heap_push(heap_t* heap, void* item);
extern void* heap_min(heap_t* heap);
extern void* heap_pop_min(heap_t* heap);


#endif
