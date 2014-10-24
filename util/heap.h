#ifndef _UTIL_HEAP
#define _UTIL_HEAP

#include <stddef.h>


typedef int (*heap_comparator)(void*, void*);

struct heap {
    void** heap;
    size_t size;
    size_t items;
    heap_comparator compare;
};


extern int heap_init(struct heap* heap, heap_comparator compare);
extern void heap_free(struct heap* heap);
extern int heap_empty(struct heap* heap);
extern size_t heap_size(struct heap* heap);
extern int heap_push(struct heap* heap, void* item);
extern void* heap_min(struct heap* heap);
extern void* heap_pop_min(struct heap* heap);


#endif
