#include "lthread.h"
#include <sys/time.h>
#include <stdio.h>

void a(void *x);

void
a(void *x)
{
    fprintf(stderr,"go lthread %p\n", lthread_current());
    int i = 3;
    struct timeval t1 = {0, 0};
    struct timeval t2 = {0, 0};
    int sleep_for = 100;
    while (i--) {
        gettimeofday(&t1, NULL);
        fprintf(stderr,"%p/%d: going to sleep for %d\n", lthread_current(), i, sleep_for);
        lthread_sleep(sleep_for);
        gettimeofday(&t2, NULL);
        fprintf(stderr,"a (%d): elapsed is: %lf\n", i,
            ((t2.tv_sec * 1000.0) + t2.tv_usec /1000.0) -
            ((t1.tv_sec * 1000.0) + t1.tv_usec/1000.0)
        );
    }
}


int
main(int argc, char **argv)
{
    lthread_spawn(a, NULL);
    //lthread_t *lt2 = NULL;
    //lthread_create(&lt2, a, NULL);
    fprintf(stderr,"run\n");
    lthread_run();
    return 0;
}
