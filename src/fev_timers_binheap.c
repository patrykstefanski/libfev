/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#include "fev_timers.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include "fev_alloc.h"
#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_ilock_impl.h"
#include "fev_time.h"

FEV_NONNULL(1, 2)
FEV_PURE static long fev_timers_cmp(const struct fev_timer *lhs, const struct fev_timer *rhs)
{
  return fev_timespec_cmp(&lhs->abs_time, &rhs->abs_time);
}

/*
 * Sift up a timer to the heap[0..len). Afterwards, heap[0..len+1) should be a
 * valid heap and contain the timer.
 */
FEV_NONNULL(1, 3)
static void fev_timers_sift_up(struct fev_timer **heap, size_t len, struct fev_timer *timer)
{
  size_t index = len;

  if (len > 0) {
    for (;;) {
      struct fev_timer *parent_ptr;

      len = (len - 1) / 2;
      parent_ptr = heap[len];
      if (fev_timers_cmp(parent_ptr, timer) <= 0)
        break;

      heap[index] = parent_ptr;
      parent_ptr->index = index;

      index = len;

      if (len == 0)
        break;
    }
  }

  heap[index] = timer;
  timer->index = index;
}

/*
 * Sift down a timer at index 'start' from the heap[0..len). Afterwards, the
 * heap[0..len-1) should be a valid heap and should not contain the timer.
 */
FEV_NONNULL(1)
static void fev_timers_sift_down(struct fev_timer **heap, size_t len, size_t start)
{
  struct fev_timer *last_ptr;

  FEV_ASSERT(len >= 1);
  FEV_ASSERT(start < len);

  if (start == len - 1)
    return;

  FEV_ASSERT(len >= 2);

  last_ptr = heap[--len];

  if (len >= 2) {
    for (;;) {
      struct fev_timer *child_ptr;
      size_t child, right;

      child = start;

      if ((len - 2) / 2 < child)
        break;

      child = child * 2 + 1;
      child_ptr = heap[child];

      /*
       * Get the index of the right child. This won't overflow, as index is less than len, and thus
       * less than SIZE_MAX.
       */
      right = child + 1;
      if (right < len && fev_timers_cmp(heap[right], child_ptr) < 0) {
        /* The right child does exist and is smaller than the left child. */
        child_ptr = heap[right];
        child = right;
      }

      if (fev_timers_cmp(last_ptr, child_ptr) <= 0)
        break;

      heap[start] = child_ptr;
      child_ptr->index = start;

      start = child;
    }
  }

  heap[start] = last_ptr;
  last_ptr->index = start;
}

FEV_NONNULL(1) static int fev_timers_bucket_grow(struct fev_timers_bucket *bucket)
{
  struct fev_timer **heap;
  size_t capacity, size;

  capacity = 2 * bucket->capacity;
  if (FEV_UNLIKELY(capacity == 0))
    capacity = 1;

  FEV_ASSERT(capacity <= SIZE_MAX / sizeof(*bucket->heap));
  size = capacity * sizeof(*bucket->heap);

  /* If fev_realloc() fails, the original heap is left untouched. */
  heap = fev_realloc(bucket->heap, size);
  if (FEV_UNLIKELY(heap == NULL))
    return -ENOMEM;

  bucket->heap = heap;
  bucket->capacity = capacity;
  return 0;
}

FEV_NONNULL(1, 2)
int fev_timers_bucket_add(struct fev_timers_bucket *bucket, struct fev_timer *timer)
{
  int min_changed;

  if (FEV_UNLIKELY(bucket->len == bucket->capacity)) {
    int ret = fev_timers_bucket_grow(bucket);
    if (FEV_UNLIKELY(ret != 0))
      return ret;
  }

  fev_timers_sift_up(bucket->heap, bucket->len++, timer);

  min_changed = timer->index == 0;
  return min_changed;
}

FEV_NONNULL(1, 2)
int fev_timers_bucket_del(struct fev_timers_bucket *bucket, struct fev_timer *timer)
{
  size_t index = timer->index;
  int min_changed;

  FEV_ASSERT(!fev_timers_bucket_empty(bucket));
  FEV_ASSERT(index != SIZE_MAX);

  fev_timers_sift_down(bucket->heap, bucket->len--, index);

  if (index > 0) {
    if (index < bucket->len)
      fev_timers_sift_up(bucket->heap, index, bucket->heap[index]);
    min_changed = 0;
  } else {
    min_changed = 1;
  }

  return min_changed;
}

FEV_NONNULL(1) void fev_timers_bucket_del_min(struct fev_timers_bucket *bucket)
{
  FEV_ASSERT(!fev_timers_bucket_empty(bucket));

  fev_timers_sift_down(bucket->heap, bucket->len--, /*start=*/0);
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

  bucket->heap = NULL;
  bucket->len = 0;
  bucket->capacity = 0;
  bucket->min = NULL;
  return 0;
}

FEV_COLD FEV_NONNULL(1) void fev_timers_bucket_fini(struct fev_timers_bucket *bucket)
{
  fev_free(bucket->heap);
  fev_timers_bucket_min_lock_fini(&bucket->min_lock);
  fev_ilock_fini(&bucket->lock);
}
