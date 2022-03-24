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


#define MINICORO_IMPL

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

#if !USE_MINICORO
static inline void _lthread_madvise(struct lthread *lt);
#endif
static inline void  _lthread_resume(struct lthread *lt);
static inline void _lthread_yield();
static inline void _lthread_free(struct lthread *lt);
static inline struct lthread* _lthread_pop_ready(struct lthread_sched *sched);
static inline int _lthread_allocate(struct lthread **new_lt, struct lthread_sched* sched);
static inline void _lthread_exit(struct lthread *lt);
static uint64_t _lthread_min_timeout(struct lthread_sched *);
static void _lthread_schedule_expired(struct lthread_sched *sched);
static inline int _lthread_sleep_cmp(struct lthread *l1, struct lthread *l2);
typedef enum {
    lthread_trace_evt_spawn = 0,
    lthread_trace_evt_exit = 1,
    lthread_trace_evt_yield = 2,
    lthread_trace_evt_resume = 3,
    lthread_trace_evt_begin_wait = 4,
    lthread_trace_evt_end_wait = 5
} lthread_trace_event_t;
static inline struct lthread_sched* _lthread_sched_create(size_t stack_size, size_t id);
static inline void* _lthread_run_sched(void* sched);
static inline void _lthread_trace_event(lthread_t* lthread, lthread_trace_event_t event);
static inline void _lthread_trace_event_unsafe(lthread_t* lt, lthread_trace_event_t event);
static inline struct lthread* _lthread_sched_pop_ready(lthread_sched_t* sched, bool block);
static inline void _lthread_sched_wake(lthread_sched_t* sched);
static inline void _lthread_sched_wake_unsafe(lthread_sched_t* sched);
static inline int _lthread_sched_isdone(lthread_sched_t* sched);
static inline bool _lthread_resume_ready(struct lthread_sched *sched);
static inline int _lthread_sched_isdone(struct lthread_sched *sched);
static inline bool _lthread_pool_begin_deep_sleep(lthread_sched_t* sched);
static inline void _lthread_pool_end_deep_sleep(lthread_sched_t* sched);
static inline lthread_sched_t* _lthread_pool_get_next(lthread_pool_state_t* pool);
static inline uint64_t _lthread_min_timeout(struct lthread_sched *sched);
static inline void _lthread_sched_push_ready(lthread_sched_t* sched, lthread_t* lt);
static inline void _lthread_sched_push_ready_unsafe(lthread_sched_t* sched, lthread_t* lt);
static inline void _lthread_desched_sleep_unsafe(struct lthread *lt);

RB_GENERATE(lthread_rb_sleep, lthread, sleep_node, _lthread_sleep_cmp);
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

    lthread_pool_state_t* pool = (lthread_pool_state_t*)calloc(1, sizeof(lthread_pool_state_t));
    memset(pool, 0, sizeof(lthread_pool_state_t));
    pool->mutex = lthread_mutex_create();
    pool->num_schedulers = num_schedulers;
    pool->asleep = (bool*)malloc(num_schedulers);
    lthread_poller_init(&pool->poller);

    lthread_sched_t* last_sched = schedulers[num_schedulers - 1];
    for (size_t i = 0; i < num_schedulers; ++i)
    {
        schedulers[i]->pool = pool;
        pool->asleep[i] = false;
        last_sched->sched_neighbor = schedulers[i];
        last_sched = schedulers[i];
    }
    _lthread_curent_sched = schedulers[0];
    pool->next = schedulers[0];
    lthread_spawn(main_func, main_arg);
    lthread_os_thread_t* threads = 0;
    if (num_aux_threads)
    {
        threads = (lthread_os_thread_t*)calloc(num_aux_threads, sizeof(lthread_os_thread_t));
        for (size_t i = 0; i < num_aux_threads; ++i)
            lthread_create_os_thread(&threads[i], _lthread_run_sched, (void*)schedulers[i + 1]);
    }
    lthread_poller_start(&pool->poller);
    _lthread_run_sched(schedulers[0]);
    for (size_t i = 0; i < num_aux_threads; ++i)
        lthread_join_os_thread(threads[i]);
    for (size_t i = 0; i < num_schedulers; ++i)
        _lthread_sched_free(schedulers[i]);
    lthread_poller_close(&pool->poller);
}

#if USE_MINICORO
void mco_entry(mco_coro* c)
{
    lthread_t* lthread = (lthread_t*)c->user_data;
    lthread->fun(lthread->arg);
}
#else
static void _exec(intptr_t ltp)
{
    struct lthread* lt = (struct lthread*)ltp;
    lt->fun(lt->arg);
    _lthread_exit(lt);
}
#endif

int lthread_create(struct lthread **new_lt, lthread_func fun, void *arg)
{
    struct lthread *lt = NULL;
    struct lthread_sched *sched = _lthread_get_sched();
    if (sched == NULL)
        return -1;
    int err = _lthread_allocate(&lt, sched);
    if (err != 0)
        return err;
    lt->is_running = false;
    lt->is_blocked = false;
    lt->fun = fun;
    lt->arg = arg;
#if USE_MINICORO
    mco_desc desc = mco_desc_init(mco_entry, sched->stack_size);
    desc.user_data = lt;
    mco_create(&lt->coro, &desc);
#else
    lt->ctx = make_fcontext(lt->stack + lt->stack_size, lt->stack_size, &_exec);
#endif

    _lthread_trace_event(lt, lthread_trace_evt_spawn);
    _lthread_sched_push_ready(sched, lt);
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

void _lthread_yield()
{
    struct lthread* lt = lthread_current();
    assert(lt->sched == _lthread_curent_sched);
    _lthread_trace_event(lt, lthread_trace_evt_yield);
#if USE_MINICORO
    mco_yield(lt->coro);
#else
    lt->sp = __builtin_frame_address(0);
    _switch(&lt->sched->ctx, &lt->ctx);
#endif
}

void _lthread_resume(struct lthread *lt)
{
    assert(lt->sched == _lthread_curent_sched);
    _lthread_trace_event(lt, lthread_trace_evt_resume);
    lthread_sched_t* sched = lt->sched;
    lthread_mutex_lock(&lt->sched->mutex);
    sched->current_lthread = lt;
    assert(lt->is_running == false);
    lt->is_running = true;
    lthread_mutex_unlock(&sched->mutex);

#if USE_MINICORO
    mco_resume(lt->coro);
#else
    _switch(&lt->ctx, &sched->ctx);
    _lthread_madvise(lt);
#endif

    lthread_mutex_lock(&sched->mutex);
    lt->is_running = false;
    sched->current_lthread = NULL;
    lt->state &= CLEARBIT(LT_ST_SLEEPING);
    lt->sleep_usecs = 0;
    if (lt->needs_resched)
    {
        TAILQ_INSERT_TAIL(&lt->sched->ready, lt, ready_next);
        lt->needs_resched = false;
    }
    if (lt->state & BIT(LT_ST_EXITED))
    {
        _lthread_trace_event_unsafe(lt, lthread_trace_evt_exit);
        _lthread_free(lt);
    }
    lthread_mutex_unlock(&sched->mutex);
}

#if !USE_MINICORO
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
#endif

void _lthread_sched_free(lthread_sched_t* sched)
{
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
    new_sched->page_size = getpagesize();
    new_sched->default_timeout = 3000000u;
    RB_INIT(&new_sched->sleeping);
    TAILQ_INIT(&new_sched->ready);
    new_sched->mutex = lthread_mutex_create();
    new_sched->cond = lthread_os_cond_create();
    new_sched->stack_size = stack_size;
    new_sched->id = id;
#if LTHREAD_TRACE
    char trace_name[128];
    snprintf(trace_name, 128, "scheduler%03zu.lrt", id);
    new_sched->trace_fd = open(trace_name, O_RDWR | O_CREAT, (mode_t)0600);
    new_sched->trace_size = 1 << 26;
    lseek(new_sched->trace_fd, new_sched->trace_size - 1, SEEK_SET);
    write(new_sched->trace_fd, "", 1);
    new_sched->trace_ptr = mmap(0, new_sched->trace_size, PROT_WRITE, MAP_SHARED, new_sched->trace_fd, 0);
    assert(new_sched->trace_ptr != MAP_FAILED);
#endif
    bzero(&new_sched->ctx, sizeof(cpu_ctx_t));
    return new_sched;
}

void _lthread_desched_event(struct lthread *lt)
{
    lthread_sched_t* sched = lt->sched;
    _lthread_trace_event(lt, lthread_trace_evt_end_wait);
    _lthread_sched_push_ready(sched, lt);
}

void _lthread_wakeup(struct lthread *lt)
{
    for(;;)
    {
        lthread_sched_t* sched = lt->sched;
        lthread_mutex_lock(&sched->mutex);
        if (lt->sched == sched)
        {
            if (lt->sleep_usecs)
            {
                _lthread_desched_sleep_unsafe(lt);
            }
            else
            {
                _lthread_sched_push_ready_unsafe(sched, lt);
            }
            lthread_mutex_unlock(&sched->mutex);
            return;
        }
        lthread_mutex_unlock(&sched->mutex);
    }
}

static inline int _lthread_sleep_cmp(struct lthread *l1, struct lthread *l2)
{
    if (l1->sleep_usecs < l2->sleep_usecs)
        return (-1);
    if (l1->sleep_usecs == l2->sleep_usecs)
        return (0);
    return (1);
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
        }
        else
        {
            sched->block_state = LTHREAD_SCHED_IS_BLOCKING;
            bool deep_sleep = _lthread_sched_isdone(sched) && !lthread_poller_has_pending_events(&sched->pool->poller);
            if (deep_sleep)
                all_done = _lthread_pool_begin_deep_sleep(sched);
            if (!all_done)
            {
                uint64_t timeout = deep_sleep ? 0 : _lthread_min_timeout(sched);
                assert(deep_sleep || timeout > 0);
                lthread_os_cond_wait(
                    &sched->cond,
                    &sched->mutex,
                    timeout
                );
                if (deep_sleep)
                    _lthread_pool_end_deep_sleep(sched);
            }
            sched->block_state = LTHREAD_SCHED_WILL_BLOCK;
        }
        lthread_mutex_unlock(&sched->mutex);
    }
    if (sched->sched_neighbor != sched)
        _lthread_sched_wake(sched->sched_neighbor);
    return 0;
}

static inline int _lthread_sched_isdone(struct lthread_sched *sched)
{
    return (
        RB_EMPTY(&sched->sleeping) &&
        TAILQ_EMPTY(&sched->ready)
    );
}

static inline bool _lthread_pool_begin_deep_sleep(lthread_sched_t* sched)
{
    lthread_pool_state_t* pool = sched->pool;
    lthread_mutex_lock(&pool->mutex);
    pool->asleep[sched->id] = true;
    bool all_done = true;
    for (size_t i = 0; all_done && i < pool->num_schedulers; ++i)
        if (pool->asleep[i] == false)
            all_done = false;
    lthread_mutex_unlock(&pool->mutex);
    return all_done;
}

static inline void _lthread_pool_end_deep_sleep(lthread_sched_t* sched)
{
    lthread_pool_state_t* pool = sched->pool;
    lthread_mutex_lock(&pool->mutex);
    pool->asleep[sched->id] = false;
    lthread_mutex_unlock(&pool->mutex);
}

static inline uint64_t _lthread_min_timeout(struct lthread_sched *sched)
{
    struct lthread *lt = NULL;
    if ((lt = RB_MIN(lthread_rb_sleep, &sched->sleeping)))
        if (lt->sleep_usecs > _lthread_usec_now())
            return lt->sleep_usecs;
    return 1;
}

/*
 * Schedules an lthread for a poller event.
 * Sets its state to LT_EV_(READ|WRITE) and inserts lthread in waiting rbtree.
 * When the event occurs, the state is cleared and node is removed by
 * _lthread_desched_event() called from lthread_run().
 */
int _lthread_wait_fd(
    int fd,
    enum lthread_event e
)
{
    struct lthread *lt = _lthread_get_sched()->current_lthread;
    _lthread_trace_event(lt, lthread_trace_evt_begin_wait);
    lthread_poller_schedule_event(
        &lt->sched->pool->poller,
        lt,
        fd,
        e
    );
    _lthread_yield();
    if (lt->state & BIT(LT_ST_FDEOF))
    {
        lt->state &= CLEARBIT(LT_ST_FDEOF);
        return -1;
    }
    else
    {
        return 0;
    }
}

void _lthread_sched_sleep(struct lthread *lt, uint64_t msecs)
{
    uint64_t usecs = msecs * 1000u;
    lthread_mutex_lock(&lt->sched->mutex);
    lt->state |= BIT(LT_ST_SLEEPING);
    if (msecs) {
        lt->sleep_usecs = _lthread_usec_now() + usecs;
        // handle colisions by increasing wakeup time
        // a min heap would probably be better
        while(RB_INSERT(lthread_rb_sleep, &lt->sched->sleeping, lt))
            ++lt->sleep_usecs;
    }
    else
    {
        lt->sleep_usecs = 0;
    }
    lthread_mutex_unlock(&lt->sched->mutex);
    _lthread_yield();
}

static inline void _lthread_desched_sleep_unsafe(struct lthread *lt)
{
    RB_REMOVE(lthread_rb_sleep, &lt->sched->sleeping, lt);
    _lthread_sched_push_ready_unsafe(lt->sched, lt);
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
#if !USE_MINICORO
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
#endif
    }
    lt->sched = sched;
    *new_lt = lt;
    return 0;
}

static inline void _lthread_free(struct lthread *lt)
{
#if USE_MINICORO
    mco_destroy(lt->coro);
    free(lt);
#else
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
#endif
}

static inline void _lthread_exit(struct lthread *lt)
{
    lt->state |= BIT(LT_ST_EXITED);
#if !USE_MINICORO
    lt->sp = lt->stack + lt->stack_size;
#endif
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
        if (lt->sleep_usecs <= current_usecs)
            _lthread_desched_sleep_unsafe(lt);
        else
            break;
    }
    lthread_mutex_unlock(&sched->mutex);
}

static inline void _lthread_sched_push_ready(
    lthread_sched_t* sched,
    lthread_t* lt
)
{
    lthread_mutex_lock(&sched->mutex);
    _lthread_sched_push_ready_unsafe(sched, lt);
    lthread_mutex_unlock(&sched->mutex);
}

static inline void _lthread_sched_push_ready_unsafe(
    lthread_sched_t* sched,
    lthread_t* lt
)
{
    if (lt->sched->current_lthread == lt)
    {
        // this prevents it from being scheduled by a work stealing neighbor...
        lt->needs_resched = true;
    }
    else
    {
        TAILQ_INSERT_TAIL(&lt->sched->ready, lt, ready_next);
    }
    _lthread_sched_wake_unsafe(sched);
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

static inline struct lthread* _lthread_pop_ready(struct lthread_sched *sched)
{
    lthread_t* result = _lthread_sched_pop_ready(sched, false);
    for(
        lthread_sched_t* i = sched->sched_neighbor;
        !result && i != sched;
        i = i->sched_neighbor
    )
        result = _lthread_sched_pop_ready(i, false);
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

static inline struct lthread* _lthread_sched_pop_ready(
    struct lthread_sched *sched,
    bool block
)
{
    if (block)
        lthread_mutex_lock(&sched->mutex);
    else if (!lthread_mutex_trylock(&sched->mutex))
        return NULL;
    struct lthread *result = TAILQ_FIRST(&sched->ready);
    if (result)
        TAILQ_REMOVE(&sched->ready, result, ready_next);
    lthread_mutex_unlock(&sched->mutex);
    return result;
}

static inline void _lthread_sched_wake_unsafe(
    struct lthread_sched *sched
)
{
    switch(sched->block_state)
    {
        case LTHREAD_SCHED_WILL_BLOCK:
            sched->block_state = LTHREAD_SCHED_WONT_BLOCK;
        case LTHREAD_SCHED_WONT_BLOCK:
            break;
        case LTHREAD_SCHED_IS_BLOCKING:
            _lthread_pool_end_deep_sleep(sched);
            lthread_os_cond_signal(&sched->cond);
            break;
    }
}


static inline void _lthread_sched_wake(
    struct lthread_sched *sched
)
{
    lthread_mutex_lock(&sched->mutex);
    _lthread_sched_wake_unsafe(sched);
    lthread_mutex_unlock(&sched->mutex);
}

void _lthread_pool_push_ready(
    lthread_pool_state_t* pool,
    lthread_t* lt
)
{
    _lthread_sched_push_ready(
        _lthread_pool_get_next(pool),
        lt
    );
}

#if LTHREAD_TRACE
static inline void _lthread_trace_data(lthread_sched_t* sched, const void* ptr, size_t size)
{
    if (sched->trace_offset + size <= sched->trace_size)
    {
        memcpy(sched->trace_ptr + sched->trace_offset, ptr, size);
        sched->trace_offset += size;
    }
    else
    {
        fprintf(stderr, "trace buffer exhausted\n");
    }
}
#endif

static inline void _lthread_trace_event(lthread_t* lt, lthread_trace_event_t event)
{
#if LTHREAD_TRACE
    lthread_sched_t* sched = lt->sched;
    lthread_mutex_lock(&sched->mutex);
    _lthread_trace_event_unsafe(lt, event);
    lthread_mutex_unlock(&sched->mutex);
#endif
}

static inline void _lthread_trace_event_unsafe(lthread_t* lt, lthread_trace_event_t event)
{
#if LTHREAD_TRACE
    assert(lt);
    lthread_sched_t* sched = lt->sched;
    uint8_t ievent = (uint8_t)event;
    uint64_t now = _lthread_usec_now();
    _lthread_trace_data(sched, &lt, sizeof(lthread_t*));
    _lthread_trace_data(sched, &now, sizeof(now));
    _lthread_trace_data(sched, &ievent, sizeof(ievent));
    _lthread_trace_data(sched, &lt->fd_wait, sizeof(lt->fd_wait));
    *(size_t*)(sched->trace_ptr + sched->trace_offset) = 0;
#endif
}

static inline lthread_sched_t* _lthread_pool_get_next(lthread_pool_state_t* pool)
{
    lthread_sched_t* next;
    lthread_mutex_lock(&pool->mutex);
    next = pool->next;
    if (next->sched_neighbor)
        pool->next = next->sched_neighbor;
    lthread_mutex_unlock(&pool->mutex);
    return next;
}

