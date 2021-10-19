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
#include <stdio.h>
#include <assert.h>

static inline void _lthread_poller_flush_events(void);

int _lthread_poller_create(void)
{
    return kqueue();
}

int _lthread_poller_ev_get_fd(struct kevent *ev)
{
    return ev->ident;
}

int _lthread_poller_ev_get_event(struct kevent *ev)
{
    return ev->filter;
}

int _lthread_poller_ev_is_eof(struct kevent *ev)
{
    return ev->flags & EV_EOF;
}

int _lthread_poller_ev_is_write(struct kevent *ev)
{
    return ev->filter == EVFILT_WRITE;
}

int _lthread_poller_ev_is_read(struct kevent *ev)
{
    return ev->filter == EVFILT_READ;
}

int _lthread_poller_poll(struct lthread_sched* sched, struct timespec t)
{
    return kevent(
        sched->poller_fd,
        sched->changelist,
        sched->nchanges,
        sched->eventlist,
        LT_MAX_EVENTS,
        &t
    );
    sched->nchanges = 0;
}

void _lthread_poller_ev_register_rd(int fd)
{
    struct lthread_sched *sched = _lthread_get_sched();
    if (sched->nchanges == LT_MAX_EVENTS)
        _lthread_poller_flush_events();
    EV_SET(
        &sched->changelist[sched->nchanges++],
        fd,
        EVFILT_READ,
        EV_ADD | EV_ENABLE | EV_ONESHOT, 
        0,
        0,
        sched->current_lthread
    );
}

void _lthread_poller_ev_register_wr(int fd)
{
    struct lthread_sched *sched = _lthread_get_sched();
    if (sched->nchanges == LT_MAX_EVENTS)
        _lthread_poller_flush_events();
    EV_SET(
        &sched->changelist[sched->nchanges++],
        fd,
        EVFILT_WRITE,
        EV_ADD | EV_ENABLE | EV_ONESHOT,
        0,
        0,
        sched->current_lthread
    );
}

void _lthread_poller_ev_clear_rd(int fd)
{
    struct kevent change;
    struct lthread_sched *sched = _lthread_get_sched();
    EV_SET(
        &change,
        fd,
        EVFILT_READ,
        EV_DELETE,
        0,
        0,
        0
    );
    assert(kevent(sched->poller_fd, &change, 1, 0, 0, 0) != -1);
}

void _lthread_poller_ev_clear_wr(int fd)
{
    struct kevent change;
    struct lthread_sched *sched = _lthread_get_sched();
    EV_SET(
        &change,
        fd,
        EVFILT_WRITE,
        EV_DELETE,
        0,
        0,
        0
    );
    assert(kevent(sched->poller_fd, &change, 1, 0, 0, 0) != -1);
}

void _lthread_poller_ev_register_trigger()
{
    struct lthread_sched *sched = _lthread_get_sched();
    struct kevent change;
    EV_SET(
        &change,
        -1,
        EVFILT_USER,
        EV_ADD,
        0,
        0,
        0
    );
    assert(kevent(sched->poller_fd, &change, 1, 0, 0, 0) != -1);
    sched->eventfd =  -1;
}

void _lthread_poller_ev_trigger(struct lthread_sched *sched)
{
    struct kevent change;
    struct timespec tm = {0, 0};
    EV_SET(
        &change,
        -1,
        EVFILT_USER,
        0,
        NOTE_TRIGGER,
        0,
        0
    );
    assert(kevent(sched->poller_fd, &change, 1, 0, 0, &tm) != -1);
}

void _lthread_poller_ev_clear_trigger()
{
    struct lthread_sched *sched = _lthread_get_sched();
    struct kevent change;
    struct timespec tm = {0, 0};
    EV_SET(
        &change,
        -1,
        EVFILT_USER,
        0,
        EV_CLEAR,
        0,
        0
    );
    assert(kevent(sched->poller_fd, &change, 1, 0, 0, &tm) != -1);
}

static inline void _lthread_poller_flush_events(void)
{
    struct lthread_sched *sched = _lthread_get_sched();
    struct timespec tm = {0, 0};
    assert(
        kevent(
            sched->poller_fd,
            sched->changelist,
            sched->nchanges,
            0,
            0,
            &tm
        ) == 0
    );
    sched->nchanges = 0;
}

