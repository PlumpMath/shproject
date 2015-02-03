/*
 * Simple locking primitives
 */
#ifndef _PLATFORM_LOCK_H
#define _PLATFORM_LOCK_H


#ifdef LOCK_POSIX

#include <pthread.h>


struct mutex {
    pthread_mutex_t lock;
};


#define __CHECK(expr)                                       \
    do {                                                    \
        int __result = (expr);                              \
        assert(__result == 0);                              \
    } while (0)

#define mutex_init(m)    __CHECK(pthread_mutex_init(&(m)->lock, NULL))
#define mutex_lock(m)    __CHECK(pthread_mutex_lock(&(m)->lock))
#define mutex_unlock(m)  __CHECK(pthread_mutex_unlock(&(m)->lock))
#define mutex_destroy(m) __CHECK(pthread_mutex_destroy(&(m)->lock))

#define mutex_trylock(m)                                    \
    ({                                                      \
        int __result = pthread_mutex_trylock(&(m)->lock));  \
        assert(__result == 0 || __result == EBUSY);         \
        __result == 0;                                      \
    })


#elif LOCK_WINDOWS


struct mutex {
    CRITICAL_SECTION lock;
};


#define mutex_init(m)    InitializeCriticalSection(&(m)->lock)
#define mutex_lock(m)    EnterCriticalSection(&(m)->lock)
#define mutex_unlock(m)  LeaveCriticalSection(&(m)->lock)
#define mutex_destroy(m) DeleteCriticalSection(&(m)->lock)
#define mutex_trylock(m) (TryEnterCriticalSection(&(m)->lock) == TRUE)


#else

#error Missing lock definitions!

#endif


#endif
