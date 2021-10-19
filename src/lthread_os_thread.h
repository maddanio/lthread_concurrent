#pragma once

#include <pthread.h>

typedef pthread_t lthread_os_thread_t;
typedef void*(*lthread_os_thread_func_t)(void*);
typedef void* lthread_os_thread_arg_t;

inline int lthread_create_os_thread(
    lthread_os_thread_t* result,
    lthread_os_thread_func_t func,
    lthread_os_thread_arg_t arg
)
{
    return pthread_create(result, 0, func, arg);
}
