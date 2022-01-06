#pragma once

#include <pthread.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <inttypes.h>

typedef pthread_mutex_t lthread_mutex_t;
typedef pthread_cond_t lthread_os_cond_t;

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

static inline lthread_os_cond_t lthread_os_cond_create()
{
    lthread_os_cond_t cond;
    int err = pthread_cond_init(&cond, 0);
    assert(err == 0);
    return cond;
}

static inline void lthread_os_cond_wait(
    lthread_os_cond_t* cond,
    lthread_mutex_t* mutex,
    uint64_t timeout_usec
)
{
    int err;
    if (timeout_usec)
    {
        struct timespec t = {0, 0};
        t.tv_sec =  timeout_usec / 1000000u;
        t.tv_nsec  =  (timeout_usec % 1000000u)  * 1000u;
        err = pthread_cond_timedwait(cond, mutex, &t);
    }
    else
    {
        err = pthread_cond_wait(cond, mutex);
    }
    assert(err == 0);
}

static inline void lthread_os_cond_signal(
    lthread_os_cond_t* cond
)
{
    int err = pthread_cond_signal(cond);
    assert(err == 0);
}