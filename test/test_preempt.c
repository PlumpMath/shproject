#include <scheduler.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>


void* coro(void* unused) {
    static int num = 1;
    int current = num++;

    fprintf(stderr, "Coroutine %d running\n", current);
    for (;;) {
    }
}


int main() {
    struct coroutine* others[5];
    for (int i = 0; i < 5; i++) {
        others[i] = sched_new_coroutine(coro, NULL);
        assert(others[i] != NULL);
    }

    printf("Main finishing\n");

    for (;;) {
    }
}
