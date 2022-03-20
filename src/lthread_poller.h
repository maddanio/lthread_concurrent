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

#include "lthread_os_thread.h"
#if defined(__FreeBSD__) || defined(__APPLE__)
#include <sys/event.h>
#define POLL_EVENT_TYPE struct kevent
#else
#include <sys/epoll.h>
#define POLL_EVENT_TYPE struct epoll_event
#endif
#include <poll.h>
#include <stdbool.h>
#include "tree.h"
#include "lthread_mutex.h"
#include "lthread_os_thread.h"

struct lthread;
enum lthread_event {
    LT_EV_READ,
    LT_EV_WRITE
};

#define LT_MAX_EVENTS    (1024)

RB_HEAD(lthread_rb_wait, lthread);
typedef struct lthread_rb_wait lthread_rb_wait_t;
RB_PROTOTYPE(lthread_rb_wait, lthread, wait_node, _lthread_wait_cmp);

typedef struct lthread_poller {
    int                 poller_fd;
#if ! (defined(__FreeBSD__) && defined(__APPLE__))
    int                 eventfd;
#endif
    POLL_EVENT_TYPE     eventlist[LT_MAX_EVENTS];
    lthread_mutex_t     mutex;
    lthread_rb_wait_t   waiting;
    lthread_os_thread_t thread;
    size_t              num_pending_events;
} lthread_poller_t;

int lthread_poller_init(lthread_poller_t* poller);
void lthread_poller_start(lthread_poller_t* poller);
void lthread_poller_close(lthread_poller_t* poller);
void lthread_poller_schedule_event(
    lthread_poller_t* poller,
    struct lthread *lt,
    int fd,
    enum lthread_event e
);
bool lthread_poller_has_pending_events(lthread_poller_t* poller);

void lthread_poller_ev_register_rd(lthread_poller_t*, int fd);
void lthread_poller_ev_register_wr(lthread_poller_t*, int fd);
void lthread_poller_ev_register_trigger(lthread_poller_t*);
void lthread_poller_ev_trigger(lthread_poller_t* poller);
void lthread_poller_ev_clear_trigger(lthread_poller_t*);

int lthread_poller_ev_get_event(POLL_EVENT_TYPE *ev);
int lthread_poller_ev_get_fd(POLL_EVENT_TYPE *ev);
int lthread_poller_ev_is_eof(POLL_EVENT_TYPE *ev);
int lthread_poller_ev_is_read(POLL_EVENT_TYPE *ev);
int lthread_poller_ev_is_write(POLL_EVENT_TYPE *ev);

int _lthread_poller_create();
int _lthread_poller_poll(lthread_poller_t* poller, struct timespec* t);

#endif
