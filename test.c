#include <coro.h>

#include <stdio.h>
#include <stdlib.h>


coroutine_t* main_coro;


void* coro(void* unused) {
    printf("Started coroutine\n");

    void* ret = (void*)1;
    while (1) {
        ret = coroutine_switch(main_coro, (void*)((int)ret + 1));
        printf("Coroutine 2 got %d\n", (int)ret);
        if ((int)ret >= 200) {
            exit(0);
        }
    }
}


int main() {
    main_coro = coroutine_self();

    coroutine_t other;
    coroutine_create(&other, coro);

    void* ret = coroutine_switch(&other, NULL);
    while (1) {
        printf("Coroutine 1 got %d\n", (int)ret);
        ret = coroutine_switch(&other, (void*)((int)ret + 1));
    }
}
