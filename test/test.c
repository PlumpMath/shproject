#include <coro.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>


struct coroutine* main_coro;


void* coro(void* unused) {
    printf("Started coroutine\n");

    for (int i = 0; i < 200; i++) {
        coroutine_switch(main_coro);
        printf("Coroutine 2\n");
        if (i >= 200) {
            exit(0);
        }
    }

    return NULL;
}


int main() {
    main_coro = coroutine_self();

    struct coroutine other;
    coroutine_create(&other, coro, NULL);

    coroutine_switch(&other);
    while (1) {
        printf("Coroutine 1\n");
        coroutine_switch(&other);
    }

    return 0;
}
