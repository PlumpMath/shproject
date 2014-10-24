#include <util/heap.h>

#include <assert.h>
#include <stdint.h>


int int_compare(void* left, void* right) {
    return (int)((intptr_t)left - (intptr_t)right);
}


int main() {
    struct heap heap;
    int result = heap_init(&heap, int_compare);
    assert(result == 0);

    assert(heap_empty(&heap));
    assert(heap_size(&heap) == 0);

    static const intptr_t VALUES[] = {12, 2955, 7, 99, 51, 1, 691050};
    static const intptr_t EXPECTED[] = {1, 7, 12, 51, 99, 2955, 691050};
    int count = sizeof(VALUES) / sizeof(VALUES[0]);

    for (int i = 0; i < count; i++) {
        result = heap_push(&heap, (void*)VALUES[i]);
        assert(result == 0);
    }

    assert(heap_size(&heap) == count);
    assert(!heap_empty(&heap));
    assert((intptr_t)heap_min(&heap) == (intptr_t)1);

    assert(heap_size(&heap) == count);

    for (int i = 0; i < count; i++) {
        intptr_t value = (intptr_t)heap_pop_min(&heap);
        assert(value == EXPECTED[i]);
    }

    assert(heap_empty(&heap));
    assert(heap_size(&heap) == 0);
    assert(heap_min(&heap) == NULL);
    assert(heap_pop_min(&heap) == NULL);

    // Repeat reordered

    static const intptr_t VALUES2[] = {2955, 12, 691050, 99, 51, 1, 7};
    for (int i = 0; i < count; i++) {
        result = heap_push(&heap, (void*)VALUES2[i]);
        assert(result == 0);
    }

    for (int i = 0; i < count; i++) {
        intptr_t value = (intptr_t)heap_pop_min(&heap);
        assert(value == EXPECTED[i]);
    }

    assert(heap_empty(&heap));
    assert(heap_size(&heap) == 0);
    assert(heap_min(&heap) == NULL);
    assert(heap_pop_min(&heap) == NULL);

    heap_free(&heap);
    return 0;
}
