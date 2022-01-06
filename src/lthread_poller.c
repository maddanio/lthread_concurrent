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

#define FD_KEY(f,e) (((int64_t)(f) << (sizeof(int32_t) * 8)) | e)
#define FD_EVENT(f) ((int32_t)(f))
#define FD_ONLY(f) ((f) >> ((sizeof(int32_t) * 8)))

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

static inline int _lthread_wait_cmp(struct lthread *l1, struct lthread *l2)
{
    if (l1->fd_wait < l2->fd_wait)
        return (-1);
    else if (l1->fd_wait == l2->fd_wait)
        return (0);
    else
        return (1);
}

RB_GENERATE(lthread_rb_wait, lthread, wait_node, _lthread_wait_cmp);

int lthread_poller_init(lthread_poller_t* poller, lthread_pool_state_t* pool)
{
    memset(poller, 0, sizeof(lthread_poller_t));
    if ((poller->poller_fd = _lthread_poller_create()) == -1)
        return -1;
    poller->pool = pool;
    lthread_poller_ev_register_trigger(poller);
    return 0;
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
    struct lthread *lt_tmp = NULL;
    assert(
        (lt->state & BIT(LT_ST_WAIT_READ)) == 0 &&
        (lt->state & BIT(LT_ST_WAIT_WRITE)) == 0
    );
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
    lt->fd_wait = FD_KEY(fd, e);
    lt_tmp = RB_INSERT(lthread_rb_wait, &poller->waiting, lt);
    assert(lt_tmp == NULL);
    lt->fd_wait = -1;
}

static inline bool lthread_poller_poll(
    lthread_poller_t* poller
)
{
    poller->num_new_events = 0;
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
        return false;
    }
}

static inline void* lthread_poller_threadfun(void* arg)
{
    lthread_poller_t* poller = (lthread_poller_t*)arg;
    while (lthread_poller_poll(poller));
    return 0;
}

void lthread_poller_start(lthread_poller_t* poller)
{
    lthread_create_os_thread(
        &poller->thread,
        lthread_poller_threadfun,
        poller
    );
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
        _lthread_pool_push_ready(poller->pool, lt);
    }
    return lt;
}

static inline lthread_t* _lthread_poller_desched_event(
    lthread_poller_t* poller,
    int fd,
    enum lthread_event e
)
{
    lthread_t find_lt;
    find_lt.fd_wait = FD_KEY(fd, e);
    lthread_t* lt = RB_FIND(lthread_rb_wait, &poller->waiting, &find_lt);
    if (lt)
        RB_REMOVE(lthread_rb_wait, &poller->waiting, lt);
    return lt;
}
