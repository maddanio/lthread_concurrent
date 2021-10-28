/*
 * Lthread
 * Copyright (C) 2012, Hasan Alayli <halayli@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * poller.c
 */


#if defined(__FreeBSD__) || defined(__APPLE__)
#include "lthread_kqueue.c"
#else
#include "lthread_epoll.c"
#endif

#include "lthread_int.h"

#include <unistd.h>

int lthread_poller_init(lthread_poller_t* poller)
{
    if ((poller->poller_fd = _lthread_poller_create()) == -1)
        return -1;
    lthread_poller_ev_register_trigger(poller);
    return 0;
}

void lthread_poller_close(lthread_poller_t* poller)
{
    close(poller->poller_fd);
#if ! (defined(__FreeBSD__) && defined(__APPLE__))
    close(poller->eventfd);
#endif    
}

size_t lthread_poller_poll(lthread_poller_t* poller, uint64_t usecs)
{
    int ret = 0;
    poller->num_new_events = 0;
    struct timespec t = {0, 0};
    if (usecs)
    {
        t.tv_sec =  usecs / 1000000u;
        if (t.tv_sec != 0)
            t.tv_nsec  =  (usecs % 1000u)  * 1000000u;
        else
            t.tv_nsec = usecs * 1000u;
    }
    do
        ret = _lthread_poller_poll(poller, t);
    while(ret == -1 && errno == EINTR);
    if (ret < 0)
    {
        perror("error adding events to epoll/kqueue");
        assert(0);
        ret = 0;
    }
    return ret;
}
