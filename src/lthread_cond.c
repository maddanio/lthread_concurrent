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
    (*c)->owner = NULL;
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
    //fprintf(stderr, "%p locking %p\n", self, c);
    lthread_mutex_lock(&c->mutex);
    if (c->owner && c->owner != self)
    {
        _lthread_cond_wait_unlock(c, self, 0);
    }
    else
    {
        c->owner = self;
        lthread_mutex_unlock(&c->mutex);
    }
    //fprintf(stderr, "%p locked %p\n", self, c);
    assert(c->owner == self);
}

int lthread_cond_wait(struct lthread_cond *c, uint64_t timeout)
{
    struct lthread *self = lthread_current();
    //fprintf(stderr, "%p waiting on %p\n", self, c);
    lthread_mutex_lock(&c->mutex);
    assert(self == c->owner);
    _lthread_cond_signal(c);
    int result = _lthread_cond_wait_unlock(c, self, timeout);
    if (result == 0)
        assert(c->owner == self);
    return result;
}

void lthread_cond_unlock_signal(struct lthread_cond *c)
{
    struct lthread *self = lthread_current();
    //fprintf(stderr, "%p unlocking %p\n", self, c);
    lthread_mutex_lock(&c->mutex);
    assert(c->owner == self);
    _lthread_cond_signal(c);
    lthread_mutex_unlock(&c->mutex);
}

size_t _lthread_cond_num_blocked(struct lthread_cond *c)
{
    struct lthread * var;
    size_t result = 0;
    TAILQ_FOREACH(var, &c->blocked_lthreads, blocked_next)
        ++result;
    return result;
}

static inline int _lthread_cond_wait_unlock(struct lthread_cond *c, lthread_t* self, uint64_t timeout)
{
    //fprintf(stderr, "%p awaiting %p\n", self, c);
    TAILQ_INSERT_TAIL(&c->blocked_lthreads, self, blocked_next);
    self->is_blocked = true;
    lthread_mutex_unlock(&c->mutex);
    lthread_sleep(timeout);
    if (self->state & BIT(LT_ST_EXPIRED))
        return (-2);
    else
        return (0);
}

static inline void _lthread_cond_signal(struct lthread_cond *c)
{
    struct lthread *lt = TAILQ_FIRST(&c->blocked_lthreads);
    //fprintf(stderr, "%p signaling %p\n", c->owner, lt);
    c->owner = lt;
    if (lt)
    {
        assert(lt->is_blocked);
        TAILQ_REMOVE(&c->blocked_lthreads, lt, blocked_next);
        lt->is_blocked = false;
        _lthread_wakeup(lt);
    }
}
