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

static inline int _lthread_cond_wait(struct lthread_cond *c, struct lthread* self, uint64_t timeout, const char* context);
static inline bool _lthread_cond_signal(struct lthread_cond *c, lthread_t* self, const char* context);
static void _lthread_cond_acquire(struct lthread_cond *c, lthread_t* self, const char* context);
static void _lthread_cond_release(struct lthread_cond *c, lthread_t* self, const char* context);

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
    lthread_mutex_lock(&c->mutex);
    assert(c->owner != self);
    if (c->owner)
        _lthread_cond_wait(c, self, 0, "lthread_cond_lock");
    else
    {
        _lthread_cond_acquire(c, self, "lthread_cond_lock");
    }
    assert(c->owner == self);
    lthread_mutex_unlock(&c->mutex);
    assert(c->owner == self);
}

int lthread_cond_wait(struct lthread_cond *c, uint64_t timeout)
{
    struct lthread *self = lthread_current();
    lthread_mutex_lock(&c->mutex);
    int result;
    _lthread_cond_signal(c, self, "lthread_cond_wait");
    result = _lthread_cond_wait(c, self, timeout, "lthread_cond_wait");
    if (result == 0)
        assert(c->owner == self);
    lthread_mutex_unlock(&c->mutex);
    return result;
}

void lthread_cond_unlock_signal(struct lthread_cond *c)
{
    struct lthread *self = lthread_current();
    lthread_mutex_lock(&c->mutex);
    _lthread_cond_signal(c, self, "lthread_cond_unlock_signal");
    lthread_mutex_unlock(&c->mutex);
}

static void _lthread_cond_acquire(struct lthread_cond *c, lthread_t* self, const char* context)
{
    assert(c->owner == NULL);
    c->owner = self;
}

static void _lthread_cond_release(struct lthread_cond *c, lthread_t* self, const char* context)
{
    assert(c->owner == self);
    c->owner = NULL;
}

size_t _lthread_cond_num_blocked(struct lthread_cond *c)
{
    struct lthread * var;
    size_t result = 0;
    TAILQ_FOREACH(var, &c->blocked_lthreads, blocked_next)
        ++result;
    return result;
}

static inline int _lthread_cond_wait(struct lthread_cond *c, lthread_t* self, uint64_t timeout, const char* context)
{
    assert(self->is_blocked == false);
    TAILQ_INSERT_TAIL(&c->blocked_lthreads, self, blocked_next);
    self->is_blocked = true;
    lthread_mutex_unlock(&c->mutex);
    lthread_sleep(timeout);
    lthread_mutex_lock(&c->mutex);
    if (c->owner != self)
    {
        assert(self->is_blocked);
        TAILQ_REMOVE(&c->blocked_lthreads, self, blocked_next);
        self->is_blocked = false;
        return -2;
    }
    else
    {
        assert(!self->is_blocked);
        return 0;
    }
}

static inline bool _lthread_cond_signal(struct lthread_cond *c, lthread_t* self, const char* context)
{
    struct lthread *lt = TAILQ_FIRST(&c->blocked_lthreads);
    _lthread_cond_release(c, self, context);
    if (lt)
    {
        assert(lt->is_blocked);
        TAILQ_REMOVE(&c->blocked_lthreads, lt, blocked_next);
        lt->is_blocked = false;
        _lthread_cond_acquire(c, lt, context);
        _lthread_wakeup(lt);
        return true;
    }
    else
    {
        return false;
    }
}
