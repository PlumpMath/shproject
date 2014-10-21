#include <coro.h>
#include <sched.h>

#include <stdio.h>
#include <stdlib.h>


coroutine_t* main_coro;


void* coro(void* unused) {
    static int num = 1;
    int current = num++;

    fprintf(stderr, "Coroutine %d running\n", current);
    for (;;) {
    }
}


int main() {
    main_coro = coroutine_self();

    coroutine_t others[5];
    for (int i = 0; i < 5; i++) {
        coroutine_create(&others[i], coro);
        sched_schedule(&others[i]);
    }

    printf("Main finishing\n");

    for (;;) {
    }
}
