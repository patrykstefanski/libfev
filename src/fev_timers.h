/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_TIMERS_H
#define FEV_TIMERS_H

#include <fev/fev.h>

#include <assert.h>
#include <stdbool.h>

#include "fev_compiler.h"
#include "fev_spinlock_impl.h"
#include "fev_thr_mutex.h"
#include "fev_time.h"
#include "fev_waiter_intf.h"

#ifdef FEV_TIMERS_MIN_LOCK_MUTEX

typedef struct fev_thr_mutex fev_timers_bucket_min_lock_t;
#define fev_timers_bucket_min_lock_init fev_thr_mutex_init
#define fev_timers_bucket_min_lock_fini fev_thr_mutex_fini
#define fev_timers_bucket_min_lock fev_thr_mutex_lock
#define fev_timers_bucket_min_unlock fev_thr_mutex_unlock

#elif defined(FEV_TIMERS_MIN_LOCK_SPINLOCK)

typedef struct fev_spinlock fev_timers_bucket_min_lock_t;
#define fev_timers_bucket_min_lock_init fev_spinlock_init
#define fev_timers_bucket_min_lock_fini fev_spinlock_fini
#define fev_timers_bucket_min_lock fev_spinlock_lock
#define fev_timers_bucket_min_unlock fev_spinlock_unlock

#endif

#if defined(FEV_TIMERS_BINHEAP)
#include "fev_timers_binheap.h"
#elif defined(FEV_TIMERS_RBTREE)
#include "fev_timers_rbtree.h"
#else
#error Wrong timers strategy selected, define either FEV_TIMERS_BINHEAP or FEV_TIMERS_RBTREE.
#endif

/* TODO: Move it to config. */
#ifndef FEV_TIMERS_BUCKETS
#define FEV_TIMERS_BUCKETS 64u
#endif

static_assert(FEV_TIMERS_BUCKETS > 0, "FEV_TIMERS_BUCKETS must be greater than 0");
static_assert((FEV_TIMERS_BUCKETS & (FEV_TIMERS_BUCKETS - 1)) == 0,
              "FEV_TIMERS_BUCKETS must be a power of 2");

#define FEV_TIMERS_BUCKET_MASK ((FEV_TIMERS_BUCKETS)-1)

/*
 * fev_timed_wait() can fail with ENOMEM if fev_timers_bucket_add() can fail.
 * fev_timers_bucket_add() fails only with ENOMEM, the function should not return any other error
 * codes.
 */
#define FEV_TIMED_WAIT_CAN_RETURN_ENOMEM FEV_TIMERS_ADD_CAN_FAIL

struct fev_timers {
  struct fev_timers_bucket buckets[FEV_TIMERS_BUCKETS];
};

FEV_NONNULL(1) FEV_PURE bool fev_timer_is_expired(const struct fev_timer *timer);

FEV_NONNULL(1) void fev_timer_set_expired(struct fev_timer *timer);

FEV_COLD FEV_NONNULL(1) int fev_timers_bucket_init(struct fev_timers_bucket *bucket);

FEV_COLD FEV_NONNULL(1) void fev_timers_bucket_fini(struct fev_timers_bucket *bucket);

FEV_NONNULL(1, 2)
int fev_timers_bucket_add(struct fev_timers_bucket *bucket, struct fev_timer *timer);

FEV_NONNULL(1, 2)
int fev_timers_bucket_del(struct fev_timers_bucket *bucket, struct fev_timer *timer);

FEV_NONNULL(1) void fev_timers_bucket_del_min(struct fev_timers_bucket *bucket);

FEV_NONNULL(1) FEV_PURE bool fev_timers_bucket_empty(const struct fev_timers_bucket *bucket);

FEV_NONNULL(1)
FEV_PURE FEV_RETURNS_NONNULL struct fev_timer *
fev_timers_bucket_min(const struct fev_timers_bucket *bucket);

FEV_COLD FEV_NONNULL(1) FEV_WARN_UNUSED_RESULT int fev_timers_init(struct fev_timers *timers);

FEV_COLD FEV_NONNULL(1) void fev_timers_fini(struct fev_timers *timers);

FEV_NONNULL(1, 2)
FEV_WARN_UNUSED_RESULT
int fev_timed_wait(struct fev_waiter *waiter, const struct timespec *abs_time);

#endif /* !FEV_TIMERS_H */
