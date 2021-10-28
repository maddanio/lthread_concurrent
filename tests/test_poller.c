#include "lthread.h"
#include "lthread_int.h"
#include "lthread_poller.h"
#include "lthread_os_thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

void my_assert(bool cond, const char* desc)
{
    if (!cond)
    {
        perror(desc);
        exit(-1);
    }
}

void test_sleep()
{
    lthread_poller_t poller = {0};
    my_assert(lthread_poller_init(&poller) == 0, "initiating poller");
    uint64_t before = _lthread_usec_now();
    lthread_poller_poll(&poller, 1000000);
    uint64_t after = _lthread_usec_now();
    fprintf(stderr, "poller slept %lluus\n", after - before);
    lthread_poller_close(&poller);
}

void* trigger_thread_fun(void* arg)
{
    lthread_poller_t* poller = (lthread_poller_t*)arg;
    sleep(1);
    lthread_poller_ev_trigger(poller);
    fprintf(stderr, "triggered poller\n");
    return 0;
}

void test_trigger()
{
    lthread_poller_t poller = {0};
    my_assert(lthread_poller_init(&poller) == 0, "initiating poller");
    lthread_os_thread_t thread;
    lthread_create_os_thread(&thread, trigger_thread_fun, &poller);
    uint64_t before = _lthread_usec_now();
    lthread_poller_poll(&poller, 2000000);
    uint64_t after = _lthread_usec_now();
    fprintf(stderr, "poller slept %lluus\n", after - before);
    lthread_poller_close(&poller);
}

int main()
{
    test_sleep();
    test_trigger();
    return 0;
}
