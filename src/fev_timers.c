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
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <queue.h>

#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_fiber.h"
#include "fev_ilock_impl.h"
#include "fev_poller.h"
#include "fev_sched_impl.h"
#include "fev_time.h"
#include "fev_waiter_impl.h"

FEV_NONNULL(1) static size_t fev_timers_hash(const struct fev_waiter *waiter)
{
  size_t h = (size_t)waiter;
  return (h >> 3) ^ (h >> 12) ^ (h >> 18) ^ (h >> 24);
}

FEV_NONNULL(1, 2)
static struct fev_timers_bucket *fev_timers_find_bucket(struct fev_timers *timers,
                                                        const struct fev_waiter *waiter)
{
  size_t index = fev_timers_hash(waiter) & FEV_TIMERS_BUCKET_MASK;
  return &timers->buckets[index];
}

FEV_NONNULL(1, 2)
static int fev_timers_add(struct fev_timers_bucket *bucket, struct fev_timer *timer)
{
  int ret, min_changed;

  fev_ilock_lock(&bucket->lock);

  ret = fev_timers_bucket_add(bucket, timer);
  if (FEV_UNLIKELY(FEV_TIMERS_ADD_CAN_FAIL && ret < 0))
    goto out;

  min_changed = ret;
  if (min_changed) {
    FEV_ASSERT(fev_timers_bucket_min(bucket) == timer);

    fev_timers_bucket_min_lock(&bucket->min_lock);
    bucket->min = timer;
    fev_timers_bucket_min_unlock(&bucket->min_lock);

    fev_poller_set_timeout(bucket, &timer->abs_time);
  }

  ret = 0;

out:
  fev_ilock_unlock_and_wake(&bucket->lock);
  return ret;
}

FEV_NONNULL(1, 2)
static void fev_timers_del(struct fev_timers_bucket *bucket, struct fev_timer *timer)
{
  struct fev_timer *min;
  int min_changed;

  fev_ilock_lock(&bucket->lock);

  /*
   * Expired timers are deleted in fev_timers_wake_expired(), thus we don't have to do anything
   * here.
   */
  if (fev_timer_is_expired(timer))
    goto out;

  min_changed = fev_timers_bucket_del(bucket, timer);
  if (!min_changed)
    goto out;

  if (fev_timers_bucket_empty(bucket))
    min = NULL;
  else
    min = fev_timers_bucket_min(bucket);

  fev_timers_bucket_min_lock(&bucket->min_lock);
  bucket->min = min;
  fev_timers_bucket_min_unlock(&bucket->min_lock);

  if (min != NULL)
    fev_poller_set_timeout(bucket, &min->abs_time);

out:
  fev_ilock_unlock_and_wake(&bucket->lock);
}

FEV_NONNULL(1, 2, 3)
static void fev_timers_wake_expired(struct fev_timers_bucket *bucket, fev_fiber_stq_head_t *fibers,
                                    uint32_t *num_fibers)
{
  struct timespec now;
  uint32_t num = 0;

  fev_clock_get_time(&now);

  while (!fev_timers_bucket_empty(bucket)) {
    struct fev_timer *min;
    struct fev_waiter *waiter;
    enum fev_waiter_wake_result result;

    min = fev_timers_bucket_min(bucket);

    if (fev_timespec_cmp(&min->abs_time, &now) > 0)
      break;

    /*
     * The timer must be deleted before waking the fiber up. See the comment in
     * fev_timers_process().
     */

    fev_timers_bucket_del_min(bucket);
    fev_timer_set_expired(min);

    waiter = min->waiter;
    result = fev_waiter_wake(waiter, FEV_WAITER_TIMED_OUT_NO_CHECK);
    if (result == FEV_WAITER_SET_AND_WAKE_UP) {
      STAILQ_INSERT_TAIL(fibers, waiter->fiber, stq_entry);
      num++;
    }
  }

  *num_fibers = num;
}

FEV_NONNULL(1)
static bool fev_timers_process(struct fev_timers_bucket *bucket, struct fev_timer *timer)
{
  fev_fiber_stq_head_t fibers = STAILQ_HEAD_INITIALIZER(fibers);
  struct fev_fiber *fiber;
  uint32_t num_fibers;
  bool expired;

  fev_ilock_lock(&bucket->lock);

  /*
   * The poller's timeout event must be disabled. Otherwise, the poller could dereference an invalid
   * pointer. Let's assume that the code below this comment does not exist and we update the min
   * element only after expiring timers. Consider the following scenario:
   * 1. The fiber X adds a timer and the timer becomes the min element.
   * 2. The poller handles the timeout event and wakes up the fiber X with reason set to
   *    FEV_TIMED_OUT_CHECK. The fiber X is now ready to run.
   * 3. The fiber Y adds an earlier timer and the timer becomes the min element. The fiber Y calls
   *    fev_waiter_wait(), but the worker A is scheduled away just before setting do_wake in
   *    fev_waiter_enable_wake_ups().
   * 4. The poller handles the timeout event, gets the pointer to the timer of the fiber Y and the
   *    worker B is scheduled away.
   * 5. The fiber X is now scheduled, it processes the timers and tries to wake the fiber Y, but it
   *    manages only to set the reason to FEV_TIMED_OUT_NO_CHECK without updating do_wake from 1 to
   *    0 (because the worker A in step 3 hasn't updated the do_wake yet).
   * 6. The worker A is now scheduled. It notices that the reason is set and thus it wakes up the
   *    fiber Y. The fiber Y is now ready to run.
   * 7. The fiber Y is now scheduled. Since its timer was deleted before setting the reason to
   *    FEV_TIMED_OUT_NO_CHECK, the fiber just exits the fev_timed_wait() function without modifying
   *    any timers state. The timer stored on stack can be now overwritten.
   * 8. The worker B is now scheduled. It still has a pointer to the timer of the fiber Y, which is
   *    now invalid.
   */
  fev_timers_bucket_min_lock(&bucket->min_lock);
  bucket->min = NULL;
  fev_timers_bucket_min_unlock(&bucket->min_lock);

  fev_timers_wake_expired(bucket, &fibers, &num_fibers);

  /*
   * A timer could be potentially woken up before it expires. For example, the poller can process a
   * timeout event with an error, but the fiber is woken up with FEV_TIMED_OUT_CHECK anyway. In that
   * case, we need to delete the timer. The caller of fev_timed_wait() will get EAGAIN and should
   * consider this as a spurious wake up.
   */
  expired = fev_timer_is_expired(timer);
  if (!expired) {
    fev_timers_bucket_del(bucket, timer);
  }

  /*
   * The min element should be NULL now, since the updates are also protected by the bucket->lock
   * and it has not been released yet. Thus, the min element doesn't have to be updated if the
   * bucket is empty.
   */
  if (!fev_timers_bucket_empty(bucket)) {
    struct fev_timer *min = fev_timers_bucket_min(bucket);

    fev_timers_bucket_min_lock(&bucket->min_lock);
    bucket->min = min;
    fev_timers_bucket_min_unlock(&bucket->min_lock);

    fev_poller_set_timeout(bucket, &min->abs_time);
  }

  fiber = fev_ilock_unlock(&bucket->lock);
  if (fiber != NULL) {
    STAILQ_INSERT_TAIL(&fibers, fiber, stq_entry);
    num_fibers++;
  }

  if (num_fibers > 0)
    fev_cur_wake_stq(&fibers, num_fibers);

  return expired;
}

FEV_NONNULL(1, 2)
FEV_WARN_UNUSED_RESULT
int fev_timed_wait(struct fev_waiter *waiter, const struct timespec *abs_time)
{
  struct fev_sched *sched;
  struct fev_timers *timers;
  struct fev_timers_bucket *bucket;
  enum fev_waiter_wake_reason reason;
  int ret;
  bool expired;

  sched = fev_cur_sched_worker->sched;
  timers = &sched->timers;

  /* This should be set by the caller. */
  FEV_ASSERT(atomic_load(&waiter->do_wake) == 0);

  bucket = fev_timers_find_bucket(timers, waiter);

  struct fev_timer timer = {
      .abs_time = *abs_time,
      .waiter = waiter,
  };

  /* Add the timer. This can block for some time. */
  ret = fev_timers_add(bucket, &timer);
  if (FEV_UNLIKELY(FEV_TIMERS_ADD_CAN_FAIL && ret < 0)) {
    /* No error except ENOMEM is possible. */
    FEV_ASSERT(ret == -ENOMEM);
    return ret;
  }

  reason = fev_waiter_wait(waiter);
  FEV_ASSERT(reason != FEV_WAITER_NONE);

  /* Most ops won't timeout probably, thus this case is likely. */
  if (FEV_LIKELY(reason == FEV_WAITER_READY)) {
    fev_timers_del(bucket, &timer);
    return 0;
  }

  if (FEV_LIKELY(reason == FEV_WAITER_TIMED_OUT_NO_CHECK)) {
    /*
     * The timer must have been deleted by fev_timers_process(), which is the only function that can
     * wake up a fiber with FEV_TIMED_OUT_NO_CHECK.
     */
    return -ETIMEDOUT;
  }

  FEV_ASSERT(reason == FEV_WAITER_TIMED_OUT_CHECK);

  expired = fev_timers_process(bucket, &timer);
  return expired ? -ETIMEDOUT : -EAGAIN;
}

FEV_COLD FEV_NONNULL(1) int fev_timers_init(struct fev_timers *timers)
{
  struct fev_timers_bucket *buckets = timers->buckets;
  size_t n;
  int ret;

  for (n = 0; n < FEV_TIMERS_BUCKETS; n++) {
    ret = fev_timers_bucket_init(&buckets[n]);
    if (FEV_UNLIKELY(ret != 0))
      goto fail;
  }

  return 0;

fail:
  while (n-- > 0)
    fev_timers_bucket_fini(&buckets[n]);

  return ret;
}

FEV_COLD FEV_NONNULL(1) void fev_timers_fini(struct fev_timers *timers)
{
  struct fev_timers_bucket *buckets = timers->buckets;

  for (size_t i = 0; i < FEV_TIMERS_BUCKETS; i++)
    fev_timers_bucket_fini(&buckets[i]);
}
