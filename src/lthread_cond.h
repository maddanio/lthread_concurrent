#pragma once

#include "lthread.h"

struct lthread_cond;
typedef struct lthread_cond lthread_cond_t;
struct lthread_lock;
typedef struct lthread_lock lthread_lock_t;

#ifdef __cplusplus
extern "C" {
#endif

int     lthread_cond_create(lthread_cond_t **c);
void    lthread_cond_free(lthread_cond_t *c);
int     lthread_cond_wait(lthread_cond_t *c, uint64_t timeout);
void    lthread_cond_signal(lthread_cond_t *c);
void    lthread_cond_broadcast(lthread_cond_t *c);

int     lthread_lock_create(struct lthread_lock **l);
void    lthread_lock_lock(struct lthread_lock *l);
int     lthread_lock_unlock(struct lthread_lock *l);

void    _lthread_cond_remove_blocked(lthread_t* lt);

#ifdef __cplusplus
}
#endif
