#include "lthread.h"
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void a(void *x);
void b(void *x);

void
b(void *x)
{
	printf("b is running\n");
    // todo: yield
    sleep(1);
	printf("b is exiting\n");
}

void
a(void *x)
{
    printf("a is running\n");
    lthread_spawn(b, NULL);
    printf("a is exiting.\n");
}


int
main(int argc, char **argv)
{
	lthread_run(a, 0, 0, 0);
	return 0;
}
