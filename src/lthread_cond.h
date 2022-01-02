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

void    lthread_cond_lock(lthread_cond_t *c);
int     lthread_cond_wait(lthread_cond_t *c, uint64_t timeout);
void    lthread_cond_unlock_signal(lthread_cond_t *c);

#ifdef __cplusplus
}
#endif
