#include <coro.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>


coroutine_t* main_coro;


void* coro(void* unused) {
    printf("Started coroutine\n");

    intptr_t ret = 1;
    while (1) {
        ret = (intptr_t)coroutine_switch(main_coro, (void*)(ret + 1));
        printf("Coroutine 2 got %"PRIdPTR"\n", ret);
        if (ret >= 200) {
            exit(0);
        }
    }
}


int main() {
    main_coro = coroutine_self();

    coroutine_t other;
    coroutine_create(&other, coro, NULL);

    intptr_t ret = (intptr_t)coroutine_switch(&other, NULL);
    while (1) {
        printf("Coroutine 1 got %"PRIdPTR"\n", ret);
        ret = (intptr_t)coroutine_switch(&other, (void*)(ret + 1));
    }
}
