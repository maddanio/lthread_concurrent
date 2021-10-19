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
 * lthread.c
 */


#define LTHREAD_MMAP_MIN (1024 * 1024)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <assert.h>
#include <inttypes.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <stdio.h>

#include "lthread.h"
#include "lthread_int.h"
#include "lthread_poller.h"

extern int errno;

static inline void _lthread_madvise(struct lthread *lt);
static inline void _lthread_free(struct lthread *lt);
static inline int _lthread_allocate(struct lthread **new_lt, struct lthread_sched* sched);
static inline void _lthread_exit(struct lthread *lt);

__thread struct lthread_sched* _lthread_curent_sched;

int _switch(cpu_ctx_t *new_ctx, cpu_ctx_t *cur_ctx)
{
    jump_fcontext(cur_ctx, *new_ctx, (intptr_t)new_ctx, false);
    return 0;
}

static void _exec(intptr_t ltp)
{
    struct lthread* lt = (struct lthread*)ltp;
    lt->fun(lt->arg);
    _lthread_exit(lt);
}

void
_lthread_yield(struct lthread *lt)
{
    assert(lt == lthread_current());
    lt->sp = __builtin_frame_address(0);
    lt->ops = 0;
    _switch(&lt->sched->ctx, &lt->ctx);
}

int _lthread_resume(struct lthread *lt)
{
    lt->sched->current_lthread = lt;
    _switch(&lt->ctx, &lt->sched->ctx);
    lt->sched->current_lthread = NULL;
    _lthread_madvise(lt);
    lt->state &= CLEARBIT(LT_ST_EXPIRED);
    if (lt->state & BIT(LT_ST_EXITED))
    {
        _lthread_free(lt);
        return -1;
    }
    else
    {
        return 0;
    }
}

static inline void _lthread_madvise(struct lthread *lt)
{
    size_t page_size = lt->sched->page_size;
    size_t current_stack_size = (lt->stack + lt->stack_size) - lt->sp;
    assert(current_stack_size <= lt->stack_size);
    if (
        current_stack_size < lt->last_stack_size &&
        (lt->last_stack_size - current_stack_size) > page_size
    )
    {
        size_t free_stack_size = lt->stack_size - current_stack_size;
        size_t free_stack_pages = free_stack_size / page_size;
        madvise(lt->stack, free_stack_pages * page_size, MADV_DONTNEED);
        lt->last_stack_size = current_stack_size;
    }
}

int lthread_init(size_t size)
{
    return _lthread_sched_create(size);
}

void _lthread_sched_free()
{
    if (_lthread_curent_sched)
    {
        close(_lthread_curent_sched->poller_fd);
    #if ! (defined(__FreeBSD__) && defined(__APPLE__))
        close(_lthread_curent_sched->eventfd);
    #endif
        free(_lthread_curent_sched);
        _lthread_curent_sched = NULL;
    }
}

int _lthread_sched_create(size_t stack_size)
{
    struct lthread_sched *new_sched;
    size_t sched_stack_size = 0;

    sched_stack_size = stack_size ? stack_size : MAX_STACK_SIZE;

    if ((new_sched = calloc(1, sizeof(struct lthread_sched))) == NULL) {
        perror("Failed to initialize scheduler\n");
        return (errno);
    }

    _lthread_curent_sched = new_sched;

    if ((new_sched->poller_fd = _lthread_poller_create()) == -1) {
        perror("Failed to initialize poller\n");
        _lthread_sched_free();
        return (errno);
    }
    _lthread_poller_ev_register_trigger();

    new_sched->stack_size = sched_stack_size;
    new_sched->page_size = getpagesize();

    new_sched->default_timeout = 3000000u;
    RB_INIT(&new_sched->sleeping);
    RB_INIT(&new_sched->waiting);
    TAILQ_INIT(&new_sched->ready);
    new_sched->mutex = lthread_mutex_create();
    bzero(&new_sched->ctx, sizeof(cpu_ctx_t));

    return (0);
}

int lthread_create(struct lthread **new_lt, lthread_func fun, void *arg)
{
    struct lthread *lt = NULL;
    struct lthread_sched *sched = _lthread_get_sched();
    if (sched == NULL) {
        _lthread_sched_create(0);
        sched = _lthread_get_sched();
        if (sched == NULL) {
            perror("Failed to create scheduler");
            return (-1);
        }
    }
    int err = _lthread_allocate(&lt, sched);
    if (err != 0)
        return err;
    lt->fun = fun;
    lt->fd_wait = -1;
    lt->arg = arg;
    lt->ctx = make_fcontext(lt->stack + lt->stack_size, lt->stack_size, &_exec);
    _lthread_push_ready(lt);
    *new_lt = lt;
    return 0;
}

lthread_t* lthread_spawn(lthread_func func, void* arg)
{
    lthread_t* result;
    int res = lthread_create(&result, func, arg);
    if (res != 0)
        return NULL;
    return result;
}

struct lthread* lthread_current()
{
    return (_lthread_get_sched()->current_lthread);
}

void lthread_yield()
{
    struct lthread *lt = lthread_current();
    _lthread_push_ready(lt);
    _lthread_yield(lt);
}

void lthread_sleep(uint64_t msecs)
{
    struct lthread *lt = lthread_current();
    _lthread_sched_sleep(lt, msecs);
}

void _lthread_renice(struct lthread *lt)
{
    if (++(lt->ops) >= 5){
        _lthread_push_ready(lt);
        _lthread_yield(lt);
    }
}

void _lthread_wakeup(struct lthread *lt)
{
    if (lt->state & BIT(LT_ST_SLEEPING)) {
        _lthread_desched_sleep(lt);
        _lthread_push_ready(lt);
    }
}

void lthread_exit()
{
    struct lthread *lt = lthread_current();
    _lthread_exit(lt);
}

void lthread_print_timestamp(char *msg)
{
	struct timeval t1 = {0, 0};
    gettimeofday(&t1, NULL);
	printf("lt timestamp: sec: %ld usec: %ld (%s)\n", t1.tv_sec, (long) t1.tv_usec, msg);
}

#define FD_KEY(f,e) (((int64_t)(f) << (sizeof(int32_t) * 8)) | e)
#define FD_EVENT(f) ((int32_t)(f))
#define FD_ONLY(f) ((f) >> ((sizeof(int32_t) * 8)))

static inline int _lthread_sleep_cmp(struct lthread *l1, struct lthread *l2);
static inline int _lthread_wait_cmp(struct lthread *l1, struct lthread *l2);

static inline int
_lthread_sleep_cmp(struct lthread *l1, struct lthread *l2)
{
    if (l1->sleep_usecs < l2->sleep_usecs)
        return (-1);
    if (l1->sleep_usecs == l2->sleep_usecs)
        return (0);
    return (1);
}

static inline int
_lthread_wait_cmp(struct lthread *l1, struct lthread *l2)
{
    if (l1->fd_wait < l2->fd_wait)
        return (-1);
    if (l1->fd_wait == l2->fd_wait)
        return (0);
    return (1);
}

RB_GENERATE(lthread_rb_sleep, lthread, sleep_node, _lthread_sleep_cmp);
RB_GENERATE(lthread_rb_wait, lthread, wait_node, _lthread_wait_cmp);

static uint64_t _lthread_min_timeout(struct lthread_sched *);

static void  _lthread_poll(struct lthread_sched* sched);
static void _lthread_schedule_expired(struct lthread_sched *sched);
static inline int _lthread_sched_isdone(struct lthread_sched *sched);

static void _lthread_poll(struct lthread_sched* sched)
{
    struct timespec t = {0, 0};
    int ret = 0;
    uint64_t usecs = 0;

    sched->num_new_events = 0;
    usecs = _lthread_min_timeout(sched);

    if (usecs && !_lthread_has_ready(sched))
    {
        t.tv_sec =  usecs / 1000000u;
        if (t.tv_sec != 0)
            t.tv_nsec  =  (usecs % 1000u)  * 1000000u;
        else
            t.tv_nsec = usecs * 1000u;
    }
    else
    {
        usecs = 0;
    }

    do
        ret = _lthread_poller_poll(sched, t);
    while(ret == -1 && errno == EINTR);
    if (ret == -1)
    {
        perror("error adding events to epoll/kqueue");
        assert(0);
    }

    sched->nevents = 0;
    sched->num_new_events = ret;
}

static inline uint64_t _sched_live_usecs(struct lthread_sched *sched)
{
    return _lthread_usec_now();
}

static uint64_t
_lthread_min_timeout(struct lthread_sched *sched)
{
    struct lthread *lt = NULL;
    if ((lt = RB_MIN(lthread_rb_sleep, &sched->sleeping)))
    {
        uint64_t current_usecs = _sched_live_usecs(sched);
        if (lt->sleep_usecs > current_usecs)
            return lt->sleep_usecs - current_usecs;
        else
            return 0;
    }
    else
    {
        return sched->default_timeout;
    }
}

/*
 * Returns 0 if there is a pending job in scheduler or 1 if done and can exit.
 */
static inline int
_lthread_sched_isdone(struct lthread_sched *sched)
{
    return (
        RB_EMPTY(&sched->waiting) &&
        RB_EMPTY(&sched->sleeping) &&
        !_lthread_has_ready(sched)
    );
}

static inline struct lthread* _lthread_handle_event(
    struct lthread_sched *sched,
    int fd,
    enum lthread_event ev,
    bool is_eof
)
{
    struct lthread* lt = _lthread_desched_event(fd, ev);                                 
    if (lt != NULL)
    {
        if (lt->state & BIT(LT_ST_WAIT_MULTI))
        {
            if (lt->ready_fds == 0)
                _lthread_push_ready(lt);
            _lthread_poller_set_fd_ready(lt, fd, ev, is_eof);
        }
        else
        {
            if (is_eof)
                lt->state |= BIT(LT_ST_FDEOF);
            _lthread_push_ready(lt);
        }                                                                   
    }
    return lt;
}

static inline void _lthread_resume_ready(struct lthread_sched *sched)
{
    struct lthread *lt = NULL;
    if ((lt = _lthread_pop_ready(sched)))
    {
        _lthread_resume(lt);
    }
}

static inline void _lthread_handle_events(struct lthread_sched *sched)
{
    int p = 0;
    int fd = 0;
    int is_eof = 0;
    while (sched->num_new_events) {
        p = --sched->num_new_events;

        fd = _lthread_poller_ev_get_fd(&sched->eventlist[p]);

        /* 
         * We got signaled via trigger to wakeup from polling
         */
        if (fd == sched->eventfd) {
            _lthread_poller_ev_clear_trigger();
            continue;
        }

        is_eof = _lthread_poller_ev_is_eof(&sched->eventlist[p]);
        if (is_eof)
            errno = ECONNRESET;

        struct lthread* lt_read = _lthread_handle_event(sched, fd, LT_EV_READ, is_eof);
        struct lthread* lt_write = _lthread_handle_event(sched, fd, LT_EV_READ, is_eof);

        assert(lt_write != NULL || lt_read != NULL);
    }
}

void lthread_run(void)
{
    struct lthread_sched *sched = _lthread_get_sched();
    if (sched == NULL)
        return;
    while (!_lthread_sched_isdone(sched)) {
        _lthread_schedule_expired(sched);
        _lthread_resume_ready(sched);
        // todo: only do this if there is fd action
        // if no fd action and sleeping threads use nanosleep
        if (false)//!_lthread_has_ready(sched) || !RB_EMPTY(&sched->waiting) || !RB_EMPTY(&sched->sleeping))
        {
            fprintf(stderr, "poll\n");
            _lthread_poll(sched);
            _lthread_handle_events(sched);
        }
    }
    _lthread_sched_free();
    return;
}

/*
 * Cancels registered event in poller and deschedules (fd, ev) -> lt from
 * rbtree. This is safe to be called even if the lthread wasn't waiting on an
 * event.
 */
void _lthread_cancel_event(struct lthread *lt)
{
    if (lt->state & BIT(LT_ST_WAIT_READ)) {
        _lthread_poller_ev_clear_rd(FD_ONLY(lt->fd_wait));
        lt->state &= CLEARBIT(LT_ST_WAIT_READ);
    } else if (lt->state & BIT(LT_ST_WAIT_WRITE)) {
        _lthread_poller_ev_clear_wr(FD_ONLY(lt->fd_wait));
        lt->state &= CLEARBIT(LT_ST_WAIT_WRITE);
    } else if (lt->state & BIT(LT_ST_WAIT_MULTI)) {
        for (nfds_t i = 0; i < lt->nfds; ++i)
            if (lt->pollfds[i].events & POLLIN)
                _lthread_desched_event(lt->pollfds[i].fd, LT_EV_READ);
            else if (lt->pollfds[i].events & POLLOUT)
                _lthread_desched_event(lt->pollfds[i].fd, LT_EV_WRITE);
            else
                assert(0);
    }
    if (lt->fd_wait >= 0)
    {
        _lthread_desched_event(FD_ONLY(lt->fd_wait), FD_EVENT(lt->fd_wait));
    }
    lt->fd_wait = -1;
}

/*
 * Deschedules an event by removing the (fd, ev) -> lt node from rbtree.
 * It also deschedules the lthread from sleeping in case it was in sleeping
 * tree.
 */
struct lthread* _lthread_desched_event(int fd, enum lthread_event e)
{
    struct lthread *lt = NULL;
    struct lthread_sched *sched = _lthread_get_sched();
    struct lthread find_lt;
    find_lt.fd_wait = FD_KEY(fd, e);
    if ((lt = RB_FIND(lthread_rb_wait, &sched->waiting, &find_lt))) {
        RB_REMOVE(lthread_rb_wait, &lt->sched->waiting, lt);
        _lthread_desched_sleep(lt);
    }
    return (lt);
}

/*
 * Schedules an lthread for a poller event.
 * Sets its state to LT_EV_(READ|WRITE) and inserts lthread in waiting rbtree.
 * When the event occurs, the state is cleared and node is removed by 
 * _lthread_desched_event() called from lthread_run().
 *
 * If event doesn't occur and lthread expired waiting, _lthread_cancel_event()
 * must be called.
 */
void _lthread_sched_event(
    struct lthread *lt,
    int fd,
    enum lthread_event e,
    uint64_t timeout
)
{
    struct lthread *lt_tmp = NULL;
    enum lthread_st st;
    if (lt->state & BIT(LT_ST_WAIT_READ) || lt->state & BIT(LT_ST_WAIT_WRITE)) {
        printf("Unexpected event. lt %p fd %"PRId64" already in %"PRId32" state\n",
            lt, lt->fd_wait, lt->state);
        assert(0);
    }

    if (e == LT_EV_READ) {
        st = LT_ST_WAIT_READ;
        _lthread_poller_ev_register_rd(fd);
    } else if (e == LT_EV_WRITE) {
        st = LT_ST_WAIT_WRITE;
        _lthread_poller_ev_register_wr(fd);
    } else {
        assert(0);
    }

    lt->state |= BIT(st);
    lt->fd_wait = FD_KEY(fd, e);
    lt_tmp = RB_INSERT(lthread_rb_wait, &lt->sched->waiting, lt);
    assert(lt_tmp == NULL);
    if (timeout == -1)
        return;
    _lthread_sched_sleep(lt, timeout);
    lt->fd_wait = -1;
    lt->state &= CLEARBIT(st);
}

/*
 * Removes lthread from sleeping rbtree.
 * This can be called multiple times on the same lthread regardless if it was
 * sleeping or not.
 */
void _lthread_desched_sleep(struct lthread *lt)
{
    if (lt->state & BIT(LT_ST_SLEEPING)) {
        RB_REMOVE(lthread_rb_sleep, &lt->sched->sleeping, lt);
        lt->state &= CLEARBIT(LT_ST_SLEEPING);
        lt->state &= CLEARBIT(LT_ST_EXPIRED);
    }
}

/*
 * Schedules lthread to sleep for `msecs` by inserting lthread into sleeping
 * rbtree and setting the lthread state to LT_ST_SLEEPING.
 * lthread state is cleared upon resumption or expiry.
 */
void _lthread_sched_sleep(struct lthread *lt, uint64_t msecs)
{
    uint64_t usecs = msecs * 1000u;
    lt->sleep_usecs = _sched_live_usecs(lt->sched) + usecs;
    if (msecs) {
        // handle colisions by increasing wakeup time
        // a min heap would probably be better
        while(RB_INSERT(lthread_rb_sleep, &lt->sched->sleeping, lt))
            ++lt->sleep_usecs;
    }
    lt->state |= BIT(LT_ST_SLEEPING);
    _lthread_yield(lt);
    lt->state &= CLEARBIT(LT_ST_SLEEPING);
    lt->sleep_usecs = 0;
}

static inline int _lthread_allocate(struct lthread **new_lt, struct lthread_sched* sched)
{
    struct lthread *lt = NULL;
    if (sched->lthread_cache_size > 0)
    {
        lt = sched->lthread_cache[--sched->lthread_cache_size];
    }
    else
    {
        if ((lt = calloc(1, sizeof(struct lthread))) == NULL) {
            perror("Failed to allocate memory for new lthread");
            return (errno);
        }
        if (sched->stack_size >= LTHREAD_MMAP_MIN)
        {
            lt->stack = mmap(0, sched->stack_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        }
        else if (posix_memalign(&lt->stack, getpagesize(), sched->stack_size))
        {
            free(lt);
            perror("Failed to allocate stack for new lthread");
            return (errno);
        }
    }
    lt->stack_size = sched->stack_size;
    lt->sched = sched;
    *new_lt = lt;
    return 0;
}

static inline void _lthread_free(struct lthread *lt)
{
    if (lt->sched->lthread_cache_size < LTHREAD_CACHE_SIZE)
    {
        lt->sched->lthread_cache[lt->sched->lthread_cache_size++] = lt;
    }
    else
    {
        if (lt->stack_size >= LTHREAD_MMAP_MIN)
            munmap(lt->stack, lt->stack_size);
        else
            free(lt->stack);
        free(lt);
    }
}

static inline void _lthread_exit(struct lthread *lt)
{
    lt->state |= BIT(LT_ST_EXITED);
    lt->sp = lt->stack + lt->stack_size;
    _lthread_yield(lt);
}

/*
 * Resumes expired lthread and cancels its events whether it was waiting
 * on one or not, and deschedules it from sleeping rbtree in case it was
 * sleeping.
 */
static void _lthread_schedule_expired(struct lthread_sched *sched)
{
    struct lthread *lt = NULL;
    uint64_t current_usecs = _sched_live_usecs(sched);
    while ((lt = RB_MIN(lthread_rb_sleep, &sched->sleeping)) != NULL) {
        if (lt->sleep_usecs <= current_usecs) {
            _lthread_cancel_event(lt);
            _lthread_desched_sleep(lt);
            lt->state |= BIT(LT_ST_EXPIRED);
            _lthread_resume(lt);
            continue;
        }
        break;
    }
}
