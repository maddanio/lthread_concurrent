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
 * lthread_poller.h
 */


#ifndef LTHREAD_POLLER_H
#define LTHREAD_POLLER_H 

#if defined(__FreeBSD__) || defined(__APPLE__)
#include <sys/event.h>
#define POLL_EVENT_TYPE struct kevent
#else
#include <sys/epoll.h>
#define POLL_EVENT_TYPE struct epoll_event
#endif
#include <poll.h>

struct lthread_sched;
struct lthread;
enum lthread_event {
    LT_EV_READ,
    LT_EV_WRITE
};

#define LT_MAX_EVENTS    (1024)

typedef struct lthread_poller {
    int                 poller_fd;
#if defined(__FreeBSD__) || defined(__APPLE__)
    struct kevent       changelist[LT_MAX_EVENTS];
#endif
    int                 eventfd;
    POLL_EVENT_TYPE     eventlist[LT_MAX_EVENTS];
    int                 nchanges;
    int                 num_new_events;    
} lthread_poller_t;

size_t lthread_poller_poll(lthread_poller_t* poller, uint64_t usecs);
int lthread_poller_init(lthread_poller_t* poller);
void lthread_poller_close(lthread_poller_t* poller);

int _lthread_poller_create();
int _lthread_poller_poll(lthread_poller_t* poller, struct timespec t);
void _lthread_poller_ev_register_rd(lthread_poller_t*, int fd);
void _lthread_poller_ev_register_wr(lthread_poller_t*, int fd);
void _lthread_poller_ev_clear_wr(lthread_poller_t*, int fd);
void _lthread_poller_ev_clear_rd(lthread_poller_t*, int fd);
void _lthread_poller_ev_register_trigger(lthread_poller_t*);
void _lthread_poller_ev_trigger(lthread_poller_t* poller);
void _lthread_poller_ev_clear_trigger(lthread_poller_t*);

int _lthread_poller_ev_get_event(POLL_EVENT_TYPE *ev);
int _lthread_poller_ev_get_fd(POLL_EVENT_TYPE *ev);
int _lthread_poller_ev_is_eof(POLL_EVENT_TYPE *ev);
int _lthread_poller_ev_is_read(POLL_EVENT_TYPE *ev);
int _lthread_poller_ev_is_write(POLL_EVENT_TYPE *ev);

#endif
