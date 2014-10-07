#include <util/heap.h>

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>


static const size_t INITIAL_SIZE = 16;


int heap_init(heap_t* heap, heap_comparator compare) {
    heap->compare = compare;
    heap->heap = (void**)malloc(sizeof(void*) * INITIAL_SIZE);
    heap->size = INITIAL_SIZE;
    heap->items = 0;

    if (heap->heap == NULL) {
        return ENOMEM;
    }
    return 0;
}


void heap_free(heap_t* heap) {
    if (heap->heap != NULL) {
        free(heap->heap);
    }
}


int heap_empty(heap_t* heap) {
    return heap->items == 0;
}


size_t heap_size(heap_t* heap) {
    return heap->items;
}


int heap_push(heap_t* heap, void* item) {
    if (heap->items + 1 >= heap->size) {
        void** new_heap = (void**)realloc(heap->heap,
            heap->size * 2 * sizeof(void*));
        if (new_heap == NULL) {
            free(heap->heap);
            heap->heap = NULL;
            return ENOMEM;
        }
        heap->heap = new_heap;
        heap->size *= 2;
    }

    heap->items++;
    heap->heap[heap->items] = item;

    size_t current = heap->items;
    while (current > 1 && heap->compare(heap->heap[current / 2], item) > 0) {
        heap->heap[current] = heap->heap[current / 2];
        current = current / 2;
    }

    heap->heap[current] = item;
    return 0;
}


void* heap_min(heap_t* heap) {
    if (heap->items == 0) {
        return NULL;
    }
    return heap->heap[1];
}


void* heap_pop_min(heap_t* heap) {
    if (heap->items == 0) {
        return NULL;
    }

    void* item = heap->heap[1];

    // Put the last item in its place
    heap->heap[1] = heap->heap[heap->items];
    heap->items--;

    void* new_root = heap->heap[1];

    int parent = 1;
    while (parent * 2 <= heap->items) {
        int left = parent * 2;
        int right = parent * 2 + 1;

        if (heap->compare(new_root, heap->heap[left]) > 0) {
            if (right > heap->items ||
                    heap->compare(heap->heap[left], heap->heap[right]) < 0) {
                heap->heap[parent] = heap->heap[left];
                parent = left;
            } else {
                heap->heap[parent] = heap->heap[right];
                parent = right;
            }
        } else if (heap->compare(new_root, heap->heap[right]) > 0) {
            heap->heap[parent] = heap->heap[right];
            parent = right;
        } else {
            break;
        }
    }
    heap->heap[parent] = new_root;

    // Resize smaller
    if (heap->items + 1 < heap->size / 4 && heap->size > INITIAL_SIZE) {
        void** new_heap = (void**)realloc(heap->heap,
            sizeof(void*) * heap->size / 2);
        if (new_heap != NULL) {
            heap->heap = new_heap;
        }
    }
    return item;
}
