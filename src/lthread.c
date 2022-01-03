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


#include <stddef.h>
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
#include "lthread_mutex.h"
#include "lthread_poller.h"
#include "lthread_os_thread.h"

extern int errno;

static inline void _lthread_madvise(struct lthread *lt);
static inline void  _lthread_resume(struct lthread *lt);
static inline void _lthread_yield();
static inline void _lthread_free(struct lthread *lt);
static inline void _lthread_push_ready(struct lthread* lt);
static inline struct lthread* _lthread_pop_ready(struct lthread_sched *sched);
static inline int _lthread_allocate(struct lthread **new_lt, struct lthread_sched* sched);
static inline void _lthread_exit(struct lthread *lt);
static uint64_t _lthread_min_timeout(struct lthread_sched *);
static size_t  _lthread_poll(struct lthread_sched* sched);
static void _lthread_schedule_expired(struct lthread_sched *sched);
static inline int _lthread_sleep_cmp(struct lthread *l1, struct lthread *l2);
static inline int _lthread_wait_cmp(struct lthread *l1, struct lthread *l2);
static inline void _lthread_handle_events(struct lthread_sched *sched, size_t num_events);
static inline struct lthread* _lthread_handle_event(
    struct lthread_sched *sched,
    int fd,
    enum lthread_event ev,
    bool is_eof
);

typedef enum {
    lthread_trace_evt_spawn,
    lthread_trace_evt_yield,
    lthread_trace_evt_resume
} lthread_trace_event_t;

static inline struct lthread_sched* _lthread_sched_create(size_t stack_size, size_t id);
static inline void* _lthread_run_sched(void* sched);
static inline void _lthread_trace_event(lthread_sched_t* sched, lthread_t* lthread, lthread_trace_event_t event);
static inline struct lthread* _lthread_sched_pop_ready(lthread_sched_t* sched, bool block);
static inline void _lthread_sched_wake(lthread_sched_t* sched);
static inline int _lthread_sched_isdone(lthread_sched_t* sched);
static void _exec(intptr_t ltp);

RB_GENERATE(lthread_rb_sleep, lthread, sleep_node, _lthread_sleep_cmp);
RB_GENERATE(lthread_rb_wait, lthread, wait_node, _lthread_wait_cmp);
#define FD_KEY(f,e) (((int64_t)(f) << (sizeof(int32_t) * 8)) | e)
#define FD_EVENT(f) ((int32_t)(f))
#define FD_ONLY(f) ((f) >> ((sizeof(int32_t) * 8)))

__thread struct lthread_sched* _lthread_curent_sched;

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

void lthread_run(lthread_func main_func, void* main_arg, size_t stack_size, size_t num_aux_threads)
{
    size_t num_schedulers = num_aux_threads + 1;
    if (stack_size == 0)
        stack_size = MAX_STACK_SIZE;
    lthread_sched_t** schedulers = (lthread_sched_t**)calloc(num_schedulers, sizeof(lthread_sched_t*));
    for (size_t i = 0; i < num_schedulers; ++i)
    {
        schedulers[i] = _lthread_sched_create(stack_size, i);
    }
    lthread_sched_t* last_sched = schedulers[num_schedulers - 1];
    lthread_pool_state_t* pool_state = (lthread_pool_state_t*)calloc(1, sizeof(lthread_pool_state_t));
    pool_state->mutex = lthread_mutex_create();
    pool_state->num_schedulers = num_schedulers;
    pool_state->num_asleep = 0;
    for (size_t i = 0; i < num_schedulers; ++i)
    {
        schedulers[i]->pool_state = pool_state;
        last_sched->sched_neighbor = schedulers[i];
        last_sched = schedulers[i];
    }
    _lthread_curent_sched = schedulers[0];
    lthread_spawn(main_func, main_arg);
    lthread_os_thread_t* threads = 0;
    if (num_aux_threads)
    {
        threads = (lthread_os_thread_t*)calloc(num_aux_threads, sizeof(lthread_os_thread_t));
        for (size_t i = 0; i < num_aux_threads; ++i)
            lthread_create_os_thread(&threads[i], _lthread_run_sched, (void*)schedulers[i + 1]);
    }
    _lthread_run_sched(schedulers[0]);
    for (size_t i = 0; i < num_aux_threads; ++i)
        lthread_join_os_thread(threads[i]);
    for (size_t i = 0; i < num_schedulers; ++i)
        _lthread_sched_free(schedulers[i]);
}

int lthread_create(struct lthread **new_lt, lthread_func fun, void *arg)
{
    struct lthread *lt = NULL;
    struct lthread_sched *sched = _lthread_get_sched();
    if (sched == NULL)
        return -1;
    int err = _lthread_allocate(&lt, sched);
    if (err != 0)
        return err;
    lt->fun = fun;
    lt->fd_wait = -1;
    lt->arg = arg;
    lt->is_running = false;
    lt->is_blocked = false;
    lt->ctx = make_fcontext(lt->stack + lt->stack_size, lt->stack_size, &_exec);
    _lthread_trace_event(sched, lt, lthread_trace_evt_spawn);
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
    struct lthread_sched* sched = _lthread_get_sched(); 
    assert(sched->current_lthread != 0);
    return sched->current_lthread;
}

void lthread_yield()
{
    lthread_t* lt = lthread_current();
    lthread_mutex_lock(&lt->sched->mutex);
    lthread_current()->needs_resched = true;
    lthread_mutex_unlock(&lt->sched->mutex);
    _lthread_yield();
}

void lthread_sleep(uint64_t msecs)
{
    struct lthread *lt = lthread_current();
    _lthread_sched_sleep(lt, msecs);
}

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

void _lthread_yield()
{
    struct lthread* lt = lthread_current();
    assert(lt->sched == _lthread_curent_sched);
    _lthread_trace_event(lt->sched, lt, lthread_trace_evt_yield);
    lt->sp = __builtin_frame_address(0);
    lt->ops = 0;
    _switch(&lt->sched->ctx, &lt->ctx);
}

void _lthread_resume(struct lthread *lt)
{
    assert(lt->sched == _lthread_curent_sched);
    _lthread_trace_event(lt->sched, lt, lthread_trace_evt_resume);
    lthread_sched_t* sched = lt->sched;
    lthread_mutex_lock(&lt->sched->mutex);
    sched->current_lthread = lt;
    lthread_mutex_unlock(&sched->mutex);
    assert(lt->is_running == false);
    lt->is_running = true;
    _switch(&lt->ctx, &sched->ctx);
    _lthread_madvise(lt);
    lt->state &= CLEARBIT(LT_ST_EXPIRED);
    lt->is_running = false;
    lthread_mutex_lock(&sched->mutex);
    sched->current_lthread = NULL;
    if (lt->needs_resched)
    {
        lt->needs_resched = false;
        TAILQ_INSERT_TAIL(&sched->ready, lt, ready_next);
    }
    else if (lt->state & BIT(LT_ST_EXITED))
    {
        _lthread_free(lt);
    }
    lthread_mutex_unlock(&sched->mutex);
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

void _lthread_sched_free(lthread_sched_t* sched)
{
    lthread_poller_close(&sched->poller);
#if LTHREAD_TRACE
    if (sched->trace_fd)
    {
        munmap(sched->trace_ptr, sched->trace_size);
        close(sched->trace_fd);
    }
#endif
    free(sched);
}

struct lthread_sched* _lthread_sched_create(size_t stack_size, size_t id)
{
    struct lthread_sched *new_sched;
    if ((new_sched = calloc(1, sizeof(lthread_sched_t))) == NULL) {
        perror("Failed to initialize scheduler\n");
        return 0;
    }
    memset(new_sched, 0, sizeof(lthread_sched_t));
    if (lthread_poller_init(&new_sched->poller) == -1) {
        perror("Failed to initialize poller\n");
        _lthread_sched_free(new_sched);
        return 0;
    }
    new_sched->page_size = getpagesize();
    new_sched->default_timeout = 3000000u;
    RB_INIT(&new_sched->sleeping);
    RB_INIT(&new_sched->waiting);
    TAILQ_INIT(&new_sched->ready);
    new_sched->mutex = lthread_mutex_create();
    new_sched->stack_size = stack_size;
    new_sched->id = id + 1;
#if LTHREAD_TRACE
    char trace_name[128];
    snprintf(trace_name, 128, "scheduler%03zu.lrt", id);
    new_sched->trace_fd = open(trace_name, O_RDWR | O_CREAT, (mode_t)0600);
    new_sched->trace_size = 1 << 20;
    lseek(new_sched->trace_fd, new_sched->trace_size - 1, SEEK_SET);
    write(new_sched->trace_fd, "", 1);
    new_sched->trace_ptr = mmap(0, new_sched->trace_size, PROT_WRITE, MAP_SHARED, new_sched->trace_fd, 0);
    assert(new_sched->trace_ptr != MAP_FAILED);
#endif
    bzero(&new_sched->ctx, sizeof(cpu_ctx_t));
    return new_sched;
}

void _lthread_renice(struct lthread *lt)
{
    if (++(lt->ops) >= 5)
        lthread_yield();
}

void _lthread_wakeup(struct lthread *lt)
{
    lthread_sched_t* sched = lt->sched;
    lthread_mutex_lock(&sched->mutex);
    if (lt->sched->current_lthread == lt)
    {
        lt->needs_resched = true;
    }
    else if ((lt->state & BIT(LT_ST_EXPIRED)) == 0)
    {
        _lthread_desched_sleep(lt);
        TAILQ_INSERT_TAIL(&lt->sched->ready, lt, ready_next);
    }
    lthread_mutex_unlock(&sched->mutex);
}

static inline int _lthread_sleep_cmp(struct lthread *l1, struct lthread *l2)
{
    if (l1->sleep_usecs < l2->sleep_usecs)
        return (-1);
    if (l1->sleep_usecs == l2->sleep_usecs)
        return (0);
    return (1);
}

static inline int _lthread_wait_cmp(struct lthread *l1, struct lthread *l2)
{
    if (l1->fd_wait < l2->fd_wait)
        return (-1);
    if (l1->fd_wait == l2->fd_wait)
        return (0);
    return (1);
}

static size_t _lthread_poll(struct lthread_sched* sched)
{
    uint64_t usecs = _lthread_has_ready(sched) ? 0 : _lthread_min_timeout(sched);
    size_t result = lthread_poller_poll(&sched->poller, usecs);
    return result;
}

static uint64_t
_lthread_min_timeout(struct lthread_sched *sched)
{
    struct lthread *lt = NULL;
    if ((lt = RB_MIN(lthread_rb_sleep, &sched->sleeping)))
    {
        uint64_t current_usecs = _lthread_usec_now();
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

static inline bool _lthread_resume_ready(struct lthread_sched *sched)
{
    struct lthread *lt = NULL;
    if ((lt = _lthread_pop_ready(sched)))
    {
        _lthread_resume(lt);
        return true;
    }
    else
    {
        return false;
    }
}

static inline void* _lthread_run_sched(void* schedp)
{
    lthread_sched_t* sched = (lthread_sched_t*)schedp;
    _lthread_curent_sched = sched;
    bool all_done = false;
    while (!all_done)
    {
        do
            _lthread_schedule_expired(sched);
        while (_lthread_resume_ready(sched));
        lthread_mutex_lock(&sched->mutex);
        if (sched->block_state == LTHREAD_SCHED_WONT_BLOCK)
        {
            sched->block_state = LTHREAD_SCHED_WILL_BLOCK;
            lthread_mutex_unlock(&sched->mutex);
        }
        else
        {
            sched->block_state = LTHREAD_SCHED_IS_BLOCKING;
            bool deep_sleep = _lthread_sched_isdone(sched);
            if (deep_sleep)
            {
                lthread_mutex_lock(&sched->pool_state->mutex);
                all_done = ++sched->pool_state->num_asleep == sched->pool_state->num_schedulers;
                lthread_mutex_unlock(&sched->pool_state->mutex);
            }
            lthread_mutex_unlock(&sched->mutex);
            if (!all_done)
            {
                size_t num_events = _lthread_poll(sched);
                if (deep_sleep)
                {
                    lthread_mutex_lock(&sched->pool_state->mutex);
                    --sched->pool_state->num_asleep;
                    lthread_mutex_unlock(&sched->pool_state->mutex);
                }
                _lthread_handle_events(sched, num_events);
            }
        }
    }
    if (sched->sched_neighbor != sched)
        _lthread_sched_wake(sched->sched_neighbor);
    return 0;
}

static inline void _lthread_handle_events(struct lthread_sched *sched, size_t num_events)
{
    int fd = 0;
    int is_eof = 0;
    for (size_t i = 0; i < num_events; ++i)
    {
        fd = lthread_poller_ev_get_fd(&sched->poller.eventlist[i]);
        if (fd == sched->poller.eventfd) {
            lthread_poller_ev_clear_trigger(&sched->poller);
            continue;
        }
        is_eof = lthread_poller_ev_is_eof(&sched->poller.eventlist[i]);
        if (is_eof)
            errno = ECONNRESET;
        struct lthread* lt_read = _lthread_handle_event(sched, fd, LT_EV_READ, is_eof);
        struct lthread* lt_write = _lthread_handle_event(sched, fd, LT_EV_READ, is_eof);
        assert(lt_write != NULL || lt_read != NULL);
    }
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
        if (is_eof)
            lt->state |= BIT(LT_ST_FDEOF);
        _lthread_push_ready(lt);
    }
    return lt;
}

/*
 * Cancels registered event in poller and deschedules (fd, ev) -> lt from
 * rbtree. This is safe to be called even if the lthread wasn't waiting on an
 * event.
 */
void _lthread_cancel_event(struct lthread *lt)
{
    if (lt->state & BIT(LT_ST_WAIT_READ)) {
        lthread_poller_ev_clear_rd(&lt->sched->poller, FD_ONLY(lt->fd_wait));
        lt->state &= CLEARBIT(LT_ST_WAIT_READ);
    } else if (lt->state & BIT(LT_ST_WAIT_WRITE)) {
        lthread_poller_ev_clear_wr(&lt->sched->poller, FD_ONLY(lt->fd_wait));
        lt->state &= CLEARBIT(LT_ST_WAIT_WRITE);
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
        lthread_poller_ev_register_rd(&lt->sched->poller, fd);
    } else if (e == LT_EV_WRITE) {
        st = LT_ST_WAIT_WRITE;
        lthread_poller_ev_register_wr(&lt->sched->poller, fd);
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
    lt->sleep_usecs = _lthread_usec_now() + usecs;
    lthread_mutex_lock(&lt->sched->mutex);
    if (msecs) {
        // handle colisions by increasing wakeup time
        // a min heap would probably be better
        while(RB_INSERT(lthread_rb_sleep, &lt->sched->sleeping, lt))
            ++lt->sleep_usecs;
    }
    lt->state |= BIT(LT_ST_SLEEPING);
    lthread_mutex_unlock(&lt->sched->mutex);
    _lthread_yield();
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
        lt->stack_size = sched->stack_size;
    }
    lt->sched = sched;
    *new_lt = lt;
    return 0;
}

static inline void _lthread_free(struct lthread *lt)
{
    if (lt->sched->lthread_cache_size < LTHREAD_CACHE_SIZE)
    {
        void* stack = lt->stack;
        size_t stack_size = lt->stack_size;
        lt->sched->lthread_cache[lt->sched->lthread_cache_size++] = lt;
        memset(lt, 0, sizeof(struct lthread));
        lt->stack = stack;
        lt->stack_size = stack_size;
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
    _lthread_yield();
}

/*
 * Resumes expired lthread and cancels its events whether it was waiting
 * on one or not, and deschedules it from sleeping rbtree in case it was
 * sleeping.
 */
static void _lthread_schedule_expired(struct lthread_sched *sched)
{
    struct lthread *lt = NULL;
    uint64_t current_usecs = _lthread_usec_now();
    lthread_mutex_lock(&sched->mutex);
    while ((lt = RB_MIN(lthread_rb_sleep, &sched->sleeping)) != NULL) {
        if (lt->sleep_usecs <= current_usecs) {
            _lthread_cancel_event(lt);
            _lthread_desched_sleep(lt);
            lt->state |= BIT(LT_ST_EXPIRED);
            TAILQ_INSERT_TAIL(&sched->ready, lt, ready_next);
            continue;
        }
        break;
    }
    lthread_mutex_unlock(&sched->mutex);
}

static inline void _lthread_push_ready(struct lthread* lt)
{
    lthread_sched_t* sched = lt->sched;
    lthread_mutex_lock(&sched->mutex);
    TAILQ_INSERT_TAIL(&sched->ready, lt, ready_next);
    lthread_mutex_unlock(&sched->mutex);
}

static inline struct lthread* _lthread_pop_ready(struct lthread_sched *sched)
{
    lthread_t* result = _lthread_sched_pop_ready(sched, false);
    if (sched->sched_neighbor)
    {
        for(
            lthread_sched_t* i = sched->sched_neighbor;
            !result && i != sched;
            i = i->sched_neighbor
        )
            result = _lthread_sched_pop_ready(i, false);
    }
    if (!result)
        result = _lthread_sched_pop_ready(sched, true);
    if (result)
    {
        if (sched->sched_neighbor != sched)
            _lthread_sched_wake(sched->sched_neighbor);
        result->sched = _lthread_curent_sched;
    }
    return result;
}

static inline void _lthread_sched_wake(
    struct lthread_sched *sched
)
{
    bool need_trigger = false;
    lthread_mutex_lock(&sched->mutex);
    switch(sched->block_state)
    {
        case LTHREAD_SCHED_WILL_BLOCK:
            sched->block_state = LTHREAD_SCHED_WONT_BLOCK;
            break;
        case LTHREAD_SCHED_WONT_BLOCK:
            break;
        case LTHREAD_SCHED_IS_BLOCKING:
            need_trigger = true;
            break;
    }
    lthread_mutex_unlock(&sched->mutex);
    if (need_trigger)
        lthread_poller_ev_trigger(&sched->poller);
}

static inline struct lthread* _lthread_sched_pop_ready(
    struct lthread_sched *sched,
    bool block
)
{
    if (block)
        lthread_mutex_lock(&sched->mutex);
    else if (!lthread_mutex_trylock(&sched->mutex))
        return 0;
    struct lthread *result = TAILQ_FIRST(&sched->ready);
    if (result)
        TAILQ_REMOVE(&sched->ready, result, ready_next);
    lthread_mutex_unlock(&sched->mutex);
    return result;
}

#if LTHREAD_TRACE
static inline void _lthread_trace_data(lthread_sched_t* sched, const void* ptr, size_t size)
{
    if (sched->trace_offset + size <= sched->trace_size)
    {
        memcpy(sched->trace_ptr, ptr, size);
        sched->trace_offset += size;
    }
}
#endif

static inline void _lthread_trace_event(lthread_sched_t* sched, lthread_t* lthread, lthread_trace_event_t event)
{
#if LTHREAD_TRACE
    uint32_t ievent = (uint32_t)event;
    _lthread_trace_data(sched, &sched->id, sizeof(sched->id));
    _lthread_trace_data(sched, &lthread, sizeof(lthread_t*));
    _lthread_trace_data(sched, &lthread->trace_cnt, sizeof(lthread->trace_cnt));
    _lthread_trace_data(sched, &ievent, sizeof(ievent));
    *(size_t*)sched->trace_ptr = 0;
    ++lthread->trace_cnt;
#endif
}
