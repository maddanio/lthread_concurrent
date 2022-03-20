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
 * lthread_kqueue.c
 */

#include "lthread_int.h"
#include "lthread_poller.h"
#include <assert.h>

int _lthread_poller_create()
{
    return kqueue();
}

int lthread_poller_ev_get_fd(struct kevent *ev)
{
    return ev->ident;
}

int lthread_poller_ev_get_event(struct kevent *ev)
{
    return ev->filter;
}

int lthread_poller_ev_is_eof(struct kevent *ev)
{
    return ev->flags & EV_EOF;
}

int lthread_poller_ev_is_write(struct kevent *ev)
{
    return ev->filter == EVFILT_WRITE;
}

int lthread_poller_ev_is_read(struct kevent *ev)
{
    return ev->filter == EVFILT_READ;
}

int _lthread_poller_poll(lthread_poller_t* poller, struct timespec* t)
{
    int result = kevent(
        poller->poller_fd,
        0,
        0,
        poller->eventlist,
        LT_MAX_EVENTS,
        t
    );
    return result;
}

//const int16_t io_flags = EV_ADD | EV_ENABLE | EV_ONESHOT;
const int16_t io_flags = EV_ADD | EV_ENABLE | EV_ONESHOT;

void lthread_poller_ev_register_rd(lthread_poller_t* poller, int fd)
{
    struct kevent change;
    EV_SET(
        &change,
        fd,
        EVFILT_READ,
        io_flags,
        0,
        0,
        0
    );
    assert(kevent(poller->poller_fd, &change, 1, 0, 0, 0) != -1);
}

void lthread_poller_ev_register_wr(lthread_poller_t* poller, int fd)
{
    struct kevent change;
    EV_SET(
        &change,
        fd,
        EVFILT_WRITE,
        io_flags,
        0,
        0,
        0
    );
    assert(kevent(poller->poller_fd, &change, 1, 0, 0, 0) != -1);
}

void lthread_poller_ev_register_trigger(lthread_poller_t* poller)
{
    struct kevent change;
    EV_SET(
        &change,
        -1,
        EVFILT_USER,
        EV_ADD,
        NOTE_FFCOPY,
        0,
        0
    );
    assert(kevent(poller->poller_fd, &change, 1, 0, 0, 0) != -1);
    poller->eventfd =  -1;
}

void lthread_poller_ev_trigger(lthread_poller_t* poller)
{
    struct kevent change;
    EV_SET(
        &change,
        -1,
        EVFILT_USER,
        EV_ENABLE,
        NOTE_FFCOPY|NOTE_TRIGGER|0x1,
        0,
        0
    );
    assert(kevent(poller->poller_fd, &change, 1, 0, 0, 0) != -1);
}

void lthread_poller_ev_clear_trigger(lthread_poller_t* poller)
{
    struct kevent change;
    EV_SET(
        &change,
        -1,
        EVFILT_USER,
        EV_DISABLE,
        EV_CLEAR,
        0,
        0
    );
    assert(kevent(poller->poller_fd, &change, 1, 0, 0, 0) != -1);
}
