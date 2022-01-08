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


#include "lthread_poller.h"
#include "lthread_mutex.h"
#include "lthread_os_thread.h"
#if defined(__FreeBSD__) || defined(__APPLE__)
#include "lthread_kqueue.c"
#else
#include "lthread_epoll.c"
#endif

#include "lthread_int.h"

#include <unistd.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>

static inline struct lthread* _lthread_poller_handle_event(
    lthread_poller_t* poller,
    int fd,
    enum lthread_event ev,
    bool is_eof
);
static inline lthread_t* _lthread_poller_desched_event(
    lthread_poller_t* poller,
    int fd,
    enum lthread_event e
);
static inline int _lthread_wait_cmp(struct lthread *l1, struct lthread *l2);
static inline void* _lthread_poller_threadfun(void* arg);

RB_GENERATE(lthread_rb_wait, lthread, wait_node, _lthread_wait_cmp);

int lthread_poller_init(lthread_poller_t* poller)
{
    memset(poller, 0, sizeof(lthread_poller_t));
    RB_INIT(&poller->waiting);
    poller->mutex = lthread_mutex_create();
    if ((poller->poller_fd = _lthread_poller_create()) == -1)
        return -1;
    return 0;
}

void lthread_poller_start(lthread_poller_t* poller)
{
    lthread_create_os_thread(
        &poller->thread,
        _lthread_poller_threadfun,
        poller
    );
}

bool lthread_poller_has_pending_events(lthread_poller_t* poller)
{
    bool result;
    lthread_mutex_lock(&poller->mutex);
    result = poller->num_pending_events == 0;
    lthread_mutex_unlock(&poller->mutex);
    return !result;
}

void lthread_poller_close(lthread_poller_t* poller)
{
    close(poller->poller_fd);
#if ! (defined(__FreeBSD__) && defined(__APPLE__))
    close(poller->eventfd);
#endif
    lthread_join_os_thread(poller->thread);
}

void lthread_poller_schedule_event(
    lthread_poller_t* poller,
    struct lthread *lt,
    int fd,
    enum lthread_event e
)
{
    assert(
        (lt->state & BIT(LT_ST_WAIT_READ)) == 0 &&
        (lt->state & BIT(LT_ST_WAIT_WRITE)) == 0
    );
    lthread_mutex_lock(&poller->mutex);
    lt->fd_wait = fd;
    struct lthread *lt_tmp = RB_INSERT(lthread_rb_wait, &poller->waiting, lt);
    assert(lt_tmp == NULL);
    ++poller->num_pending_events;
    lthread_mutex_unlock(&poller->mutex);
    switch(e)
    {
        case LT_EV_READ:
            lt->state |= BIT(LT_ST_WAIT_READ);
            lthread_poller_ev_register_rd(poller, fd);
            break;
        case LT_EV_WRITE:
            lt->state |= BIT(LT_ST_WAIT_WRITE);
            lthread_poller_ev_register_wr(poller, fd);
            break;
    }
}

static inline bool lthread_poller_poll(
    lthread_poller_t* poller
)
{
    int num_events;
    do
        num_events = _lthread_poller_poll(poller, 0);
    while(num_events == -1 && errno == EINTR);
    if (num_events >= 0)
    {
        for (size_t i = 0; i < num_events; ++i)
        {
            int fd = lthread_poller_ev_get_fd(&poller->eventlist[i]);
            int is_eof = lthread_poller_ev_is_eof(&poller->eventlist[i]);
            if (is_eof)
                errno = ECONNRESET;
            struct lthread* lt_read = _lthread_poller_handle_event(poller, fd, LT_EV_READ, is_eof);
            struct lthread* lt_write = _lthread_poller_handle_event(poller, fd, LT_EV_WRITE, is_eof);
            assert(lt_write != NULL || lt_read != NULL);
        }
        return true;
    }
    else
    {
        fprintf(stderr, "poller done\n");
        return false;
    }
}

static inline void* _lthread_poller_threadfun(void* arg)
{
    lthread_poller_t* poller = (lthread_poller_t*)arg;
    while (lthread_poller_poll(poller));
    return 0;
}

static inline struct lthread* _lthread_poller_handle_event(
    lthread_poller_t* poller,
    int fd,
    enum lthread_event ev,
    bool is_eof
)
{
    struct lthread* lt = _lthread_poller_desched_event(poller, fd, ev);
    if (lt != NULL)
    {
        if (is_eof)
            lt->state |= BIT(LT_ST_FDEOF);
        switch(ev)
        {
            case LT_EV_READ:
                lt->state &= CLEARBIT(LT_ST_WAIT_READ);
                break;
            case LT_EV_WRITE:
                lt->state &= CLEARBIT(LT_EV_WRITE);
                break;
        }
        lt->fd_wait = -1;
        _lthread_desched_event(lt);
        lthread_mutex_lock(&poller->mutex);
        --poller->num_pending_events;
        lthread_mutex_unlock(&poller->mutex);
    }
    else
    {
        switch(ev)
        {
            case LT_EV_READ:
                break;
            case LT_EV_WRITE:
                break;
        }
    }
    return lt;
}

static inline lthread_t* _lthread_poller_desched_event(
    lthread_poller_t* poller,
    int fd,
    enum lthread_event ev
)
{
    lthread_mutex_lock(&poller->mutex);
    lthread_t find_lt;
    find_lt.fd_wait = fd;
    switch(ev)
    {
        case LT_EV_READ:
            find_lt.state = BIT(LT_ST_WAIT_READ);
            break;
        case LT_EV_WRITE:
            find_lt.state = BIT(LT_EV_WRITE);
            break;
    }
    lthread_t* lt = RB_FIND(lthread_rb_wait, &poller->waiting, &find_lt);
    if (lt)
        RB_REMOVE(lthread_rb_wait, &poller->waiting, lt);
    lthread_mutex_unlock(&poller->mutex);
    return lt;
}

static inline uint32_t fd_key(struct lthread *lt)
{
    uint32_t result = (uint16_t)lt->fd_wait;
    if (lt->state & BIT(LT_ST_WAIT_READ))
        result |= ((uint64_t)LT_EV_READ) << 16;
    else
        result |= ((uint64_t)LT_EV_WRITE) << 16;
    return result;
}

static inline int _lthread_wait_cmp(struct lthread *l1, struct lthread *l2)
{
    uint32_t key1 = fd_key(l1);
    uint32_t key2 = fd_key(l2);
    if (key1 < key2)
        return -1;
    else if (key1 == key2)
        return 0;
    else
        return 1;
}
