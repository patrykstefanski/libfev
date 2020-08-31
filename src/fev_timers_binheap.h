/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_TIMERS_BINHEAP_H
#define FEV_TIMERS_BINHEAP_H

#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>

#include "fev_alloc.h"
#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_ilock_intf.h"
#include "fev_poller.h"
#include "fev_time.h"
#include "fev_waiter_intf.h"

#ifdef FEV_ASSUME_MALLOC_NEVER_FAILS
#define FEV_TIMERS_ADD_CAN_FAIL 0
#else
#define FEV_TIMERS_ADD_CAN_FAIL 1
#endif

struct fev_timer {
  struct timespec abs_time;
  size_t index;
  struct fev_waiter *waiter;
};

struct fev_timers_bucket {
  alignas(FEV_DCACHE_LINE_SIZE) struct fev_ilock lock;
  struct fev_timer **heap;
  size_t len;
  size_t capacity;
  struct fev_poller_timers_bucket_data poller_data;

  alignas(FEV_DCACHE_LINE_SIZE) fev_timers_bucket_min_lock_t min_lock;
  struct fev_timer *min;
};

FEV_NONNULL(1) FEV_PURE static inline bool fev_timer_is_expired(const struct fev_timer *timer)
{
  return timer->index == SIZE_MAX;
}

FEV_NONNULL(1) static inline void fev_timer_set_expired(struct fev_timer *timer)
{
  timer->index = SIZE_MAX;
}

FEV_NONNULL(1)
FEV_PURE static inline bool fev_timers_bucket_empty(const struct fev_timers_bucket *bucket)
{
  return bucket->len == 0;
}

FEV_NONNULL(1)
FEV_PURE FEV_RETURNS_NONNULL static inline struct fev_timer *
fev_timers_bucket_min(const struct fev_timers_bucket *bucket)
{
  struct fev_timer *min;

  FEV_ASSERT(!fev_timers_bucket_empty(bucket));
  min = bucket->heap[0];
  FEV_ASSERT(min != NULL);
  return min;
}

#endif /* !FEV_TIMERS_BINHEAP_H */
