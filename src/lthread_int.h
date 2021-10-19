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
 * lthread_int.c
 */


#ifndef LTHREAD_INT_H
#define LTHREAD_INT_H

#include <sys/time.h>
#include <sys/types.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>

#include "lthread_poller.h"
#include "lthread_mutex.h"
#include "queue.h"
#include "tree.h"
#include "libcontext.h"

#define LT_MAX_EVENTS    (1024)
#define MAX_STACK_SIZE (8 * 1024 * 1024) /* 2MB */
#define LTHREAD_CACHE_SIZE 32

#define BIT(x) (1 << (x))
#define CLEARBIT(x) ~(1 << (x))

struct lthread;
struct lthread_sched;
typedef struct lthread_sched lthread_sched_t;

TAILQ_HEAD(lthread_q, lthread);

typedef void (*lthread_func)(void *);

typedef fcontext_t cpu_ctx_t;

enum lthread_st {
    LT_ST_WAIT_READ,    /* lthread waiting for READ on socket */
    LT_ST_WAIT_WRITE,   /* lthread waiting for WRITE on socket */
    LT_ST_EXITED,       /* lthread has exited and needs cleanup */
    LT_ST_SLEEPING,     /* lthread is sleeping */
    LT_ST_EXPIRED,      /* lthread has expired and needs to run */
    LT_ST_FDEOF,        /* lthread socket has shut down */
};

struct lthread {
    cpu_ctx_t               ctx;            /* cpu ctx info */
    lthread_func            fun;            /* func lthread is running */
    void                    *arg;           /* func args passed to func */
    size_t                  stack_size;     /* current stack_size */
    size_t                  last_stack_size; /* last yield  stack_size */
    enum lthread_st         state;          /* current lthread state */
    struct lthread_sched    *sched;         /* scheduler lthread belongs to */
    int64_t                 fd_wait;        /* fd we are waiting on */
    void                    *stack;         /* ptr to lthread_stack */
    void                    *sp;            /* ptr to last stack ptr */
    uint32_t                ops;            /* num of ops since yield */
    uint64_t                sleep_usecs;    /* until when lthread is sleeping */
    RB_ENTRY(lthread)       sleep_node;     /* sleep tree node pointer */
    RB_ENTRY(lthread)       wait_node;      /* event tree node pointer */
    TAILQ_ENTRY(lthread)    ready_next;     /* ready to run list */
    TAILQ_ENTRY(lthread)    blocked_next;      /* blocked on a synchronization primitve */
    int ready_fds; /* # of fds that are ready. for poll(2) */
    struct pollfd *pollfds;
    nfds_t nfds;
};

RB_HEAD(lthread_rb_sleep, lthread);
typedef struct lthread_rb_sleep lthread_rb_sleep_t;
RB_HEAD(lthread_rb_wait, lthread);
typedef struct lthread_rb_wait lthread_rb_wait_t;
RB_PROTOTYPE(lthread_rb_wait, lthread, wait_node, _lthread_wait_cmp);

struct lthread_sched {
    // local
    cpu_ctx_t           ctx;
    size_t              stack_size;
    int                 page_size;
    uint64_t            default_timeout;
    struct lthread*     current_lthread;
    lthread_sched_t*    next_sched;

    // shared
    lthread_sched_t*    sched_neighbor;
    lthread_mutex_t     mutex;
    struct lthread_q    ready;
    struct lthread*     lthread_cache[LTHREAD_CACHE_SIZE];
    size_t              lthread_cache_size;

    // poller stuff
    lthread_rb_sleep_t  sleeping;
    lthread_rb_wait_t   waiting;
    int                 poller_fd;
#if defined(__FreeBSD__) || defined(__APPLE__)
    struct kevent       changelist[LT_MAX_EVENTS];
#endif
    int                 eventfd;
    POLL_EVENT_TYPE     eventlist[LT_MAX_EVENTS];
    int                 nchanges;
    int                 num_new_events;
};


int _lthread_sched_create(size_t stack_size);
void _lthread_wakeup(struct lthread *lt);
int _lthread_resume(struct lthread *lt);
void _lthread_renice(struct lthread *lt);
void _lthread_sched_free();
void _lthread_yield(struct lthread *lt);
void _lthread_desched_sleep(struct lthread *lt);
void _lthread_sched_sleep(struct lthread *lt, uint64_t msecs);
void _lthread_cancel_event(struct lthread *lt);
struct lthread* _lthread_desched_event(int fd, enum lthread_event e);
void _lthread_sched_event(
    struct lthread *lt,
    int fd,
    enum lthread_event e,
    uint64_t timeout
);
int _lthread_switch(cpu_ctx_t *new_ctx, cpu_ctx_t *cur_ctx);

extern __thread struct lthread_sched* _lthread_curent_sched;

static inline struct lthread_sched* _lthread_get_sched()
{
    return _lthread_curent_sched;
}

static inline uint64_t _lthread_diff_usecs(uint64_t t1, uint64_t t2)
{
    return (t2 - t1);
}

static inline uint64_t _lthread_usec_now(void)
{
    struct timeval t1 = {0, 0};
    gettimeofday(&t1, NULL);
    return (t1.tv_sec * 1000000) + t1.tv_usec;
}

static inline void _lthread_push_ready(struct lthread* lt)
{
    TAILQ_INSERT_TAIL(&lt->sched->ready, lt, ready_next);    
}

static inline struct lthread* _lthread_pop_ready(struct lthread_sched *sched)
{
    struct lthread *result = TAILQ_FIRST(&sched->ready);
    if (result)
        TAILQ_REMOVE(&sched->ready, result, ready_next);
    return result;
}

static inline bool _lthread_has_ready(struct lthread_sched *sched)
{
    return !TAILQ_EMPTY(&sched->ready);
}


#endif
