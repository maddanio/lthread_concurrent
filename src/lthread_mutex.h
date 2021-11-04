#pragma once

#include <pthread.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>

typedef pthread_mutex_t lthread_mutex_t;

static inline lthread_mutex_t lthread_mutex_create()
{
    lthread_mutex_t mutex;
    int err = pthread_mutex_init(&mutex, NULL);
    assert(err == 0);
    return mutex;
}

static inline void lthread_mutex_destroy(lthread_mutex_t* mutex)
{
    int err = pthread_mutex_destroy(mutex);
    assert(err == 0);
}

static inline void lthread_mutex_lock(lthread_mutex_t* mutex)
{
    int err = pthread_mutex_lock(mutex);    
    assert(err == 0);
}

static inline void lthread_mutex_unlock(lthread_mutex_t* mutex)
{
    int err = pthread_mutex_unlock(mutex);
    assert(err == 0);
}

static inline bool lthread_mutex_trylock(lthread_mutex_t* mutex)
{
    int result = pthread_mutex_trylock(mutex);
    if (result == 0) {
        return true;
    } else {
        assert(result == EBUSY);
        return false;
    }
}
