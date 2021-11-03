#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#include "lthread.h"
#include "lthread_int.h"
#include "lthread_mutex.h"

struct lthread_cond {
    struct lthread_q blocked_lthreads;
    lthread_mutex_t mutex;
    struct lthread* owner;
};

static inline int _lthread_cond_wait_unlock(struct lthread_cond *c, struct lthread* self, uint64_t timeout);
static inline void _lthread_cond_signal(struct lthread_cond *c);

int lthread_cond_create(struct lthread_cond **c)
{
    if ((*c = calloc(1, sizeof(struct lthread_cond))) == NULL)
        return (-1);
    TAILQ_INIT(&(*c)->blocked_lthreads);
    (*c)->mutex = lthread_mutex_create();
    return (0);
}

void lthread_cond_free(struct lthread_cond *c)
{
    lthread_mutex_destroy(&c->mutex);
    free(c);
}

void lthread_cond_lock(struct lthread_cond *c)
{
    struct lthread *self = lthread_current();
    lthread_mutex_lock(&c->mutex);
    if (c->owner && c->owner != self)
    {
        _lthread_cond_wait_unlock(c, self, 0);
        lthread_mutex_lock(&c->mutex);
    }
    assert(c->owner == NULL);
    c->owner = self;
    fprintf(stderr, "%p locked %p\n", lthread_current(), c);
    lthread_mutex_unlock(&c->mutex);
}

int lthread_cond_wait(struct lthread_cond *c, uint64_t timeout)
{
    struct lthread *self = lthread_current();
    lthread_mutex_lock(&c->mutex);
    assert(self == c->owner);
    fprintf(stderr, "%p waiting on %p\n", lthread_current(), c);
    c->owner = NULL;
    int result = _lthread_cond_wait_unlock(c, self, timeout);
    if (result == 0)
    {
        lthread_mutex_lock(&c->mutex);
        c->owner = self;
        lthread_mutex_unlock(&c->mutex);
    }
    return result;
}

void lthread_cond_signal(struct lthread_cond *c)
{
    struct lthread *lt = lthread_current();
    lthread_mutex_lock(&c->mutex);
    assert(c->owner == lt);
    c->owner = NULL;
    _lthread_cond_signal(c);
    lthread_mutex_unlock(&c->mutex);
}

void lthread_cond_unlock(struct lthread_cond *c)
{
    struct lthread *lt = lthread_current();
    lthread_mutex_lock(&c->mutex);
    assert(c->owner == lt);
    c->owner = NULL;
    _lthread_cond_signal(c);
    lthread_mutex_unlock(&c->mutex);
}

void _lthread_cond_remove_blocked(lthread_t* lt)
{
    lthread_mutex_lock(&lt->cond->mutex);
    TAILQ_REMOVE(&lt->cond->blocked_lthreads, lt, blocked_next);
    lthread_mutex_unlock(&lt->cond->mutex);
    lt->cond = 0;
}

static inline int _lthread_cond_wait_unlock(struct lthread_cond *c, struct lthread* self, uint64_t timeout)
{
    TAILQ_INSERT_TAIL(&c->blocked_lthreads, self, blocked_next);
    lthread_mutex_unlock(&c->mutex);
    self->cond = c;
    lthread_sleep(timeout);
    if (self->state & BIT(LT_ST_EXPIRED))
        return (-2);
    else
        return (0);
}

static inline void _lthread_cond_signal(struct lthread_cond *c)
{
    fprintf(stderr, "%p signaling %p\n", lthread_current(), c);
    c->owner = NULL;
    struct lthread *lt = TAILQ_FIRST(&c->blocked_lthreads);
    if (lt)
    {
        _lthread_wakeup(lt);
        TAILQ_REMOVE(&c->blocked_lthreads, lt, blocked_next);
    }
    else
    {
        fprintf(stderr, "noone listening to %p\n", c);
    }
}
