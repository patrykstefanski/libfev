/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#include "fev_timers.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tree.h>

#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_ilock_impl.h"
#include "fev_time.h"

/*
 * Entries in the rbtree implementation must be unique, comparing only timespec is not enough and
 * could crash the application. Thus, in addition, timer->waiter is compared, since it is always
 * unique at a given time. A waiter can be reused when the corresponding timer is removed.
 */
FEV_NONNULL(1, 2)
FEV_PURE static int fev_timers_cmp(const struct fev_timer *lhs, const struct fev_timer *rhs)
{
  long cmp;

  cmp = fev_timespec_cmp(&lhs->abs_time, &rhs->abs_time);
  if (cmp != 0)
    return cmp < 0 ? -1 : 1;

  /* The lhs & rhs pointers are unrelated. */
  if ((uintptr_t)lhs->waiter == (uintptr_t)rhs->waiter)
    return 0;
  if ((uintptr_t)lhs->waiter < (uintptr_t)rhs->waiter)
    return -1;
  return 1;
}

RB_GENERATE_INSERT_COLOR(fev_timers_rbtree_head, fev_timer, entry, static);
RB_GENERATE_INSERT(fev_timers_rbtree_head, fev_timer, entry, fev_timers_cmp, static);
RB_GENERATE_REMOVE_COLOR(fev_timers_rbtree_head, fev_timer, entry, static);
RB_GENERATE_REMOVE(fev_timers_rbtree_head, fev_timer, entry, static);
RB_GENERATE_NEXT(fev_timers_rbtree_head, fev_timer, entry, FEV_PURE static);

FEV_NONNULL(1, 2)
int fev_timers_bucket_add(struct fev_timers_bucket *bucket, struct fev_timer *timer)
{
  int min_changed = 0;

  /* The tree_min element should be minimal, thus it shouldn't have a left child. */
  FEV_ASSERT(bucket->tree_min == NULL || RB_LEFT(bucket->tree_min, entry) == NULL);

  timer->expired = false;

  RB_INSERT(fev_timers_rbtree_head, &bucket->head, timer);

  /* If the left child changed, then the element is not minimal anymore. */
  if (bucket->tree_min == NULL || RB_LEFT(bucket->tree_min, entry) != NULL) {
    FEV_ASSERT(bucket->tree_min == NULL || RB_LEFT(bucket->tree_min, entry) == timer);
    bucket->tree_min = timer;
    min_changed = 1;
  }

  return min_changed;
}

FEV_NONNULL(1, 2)
int fev_timers_bucket_del(struct fev_timers_bucket *bucket, struct fev_timer *timer)
{
  int min_changed = 0;

  FEV_ASSERT(!fev_timers_bucket_empty(bucket));

  if (bucket->tree_min == timer) {
    bucket->tree_min = RB_NEXT(fev_timers_rbtree_head, &bucket->head, bucket->tree_min);
    min_changed = 1;
  }

  RB_REMOVE(fev_timers_rbtree_head, &bucket->head, timer);

  return min_changed;
}

FEV_NONNULL(1) void fev_timers_bucket_del_min(struct fev_timers_bucket *bucket)
{
  struct fev_timer *tree_min;

  FEV_ASSERT(!fev_timers_bucket_empty(bucket));

  tree_min = bucket->tree_min;
  bucket->tree_min = RB_NEXT(fev_timers_rbtree_head, &bucket->head, tree_min);
  RB_REMOVE(fev_timers_rbtree_head, &bucket->head, tree_min);
}

FEV_COLD FEV_NONNULL(1) int fev_timers_bucket_init(struct fev_timers_bucket *bucket)
{
  int ret;

  ret = fev_ilock_init(&bucket->lock);
  if (FEV_UNLIKELY(ret != 0))
    return ret;

  ret = fev_timers_bucket_min_lock_init(&bucket->min_lock);
  if (FEV_UNLIKELY(ret != 0)) {
    fev_ilock_fini(&bucket->lock);
    return ret;
  }

  RB_INIT(&bucket->head);
  bucket->tree_min = NULL;
  bucket->min = NULL;
  return 0;
}

FEV_COLD FEV_NONNULL(1) void fev_timers_bucket_fini(struct fev_timers_bucket *bucket)
{
  fev_timers_bucket_min_lock_fini(&bucket->min_lock);
  fev_ilock_fini(&bucket->lock);
}
