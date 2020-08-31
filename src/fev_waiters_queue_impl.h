/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_WAITERS_QUEUE_IMPL_H
#define FEV_WAITERS_QUEUE_IMPL_H

#include "fev_waiters_queue_intf.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <queue.h>

#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_fiber.h"
#include "fev_ilock_impl.h"
#include "fev_time.h"
#include "fev_timers.h"
#include "fev_waiter_impl.h"

FEV_NONNULL(1)
FEV_WARN_UNUSED_RESULT static inline int fev_waiters_queue_init(struct fev_waiters_queue *queue)
{
  int ret = fev_ilock_init(&queue->lock);
  if (FEV_UNLIKELY(ret != 0))
    return ret;

  TAILQ_INIT(&queue->nodes);
  return 0;
}

FEV_NONNULL(1) static inline void fev_waiters_queue_fini(struct fev_waiters_queue *queue)
{
  fev_ilock_fini(&queue->lock);
}

FEV_NONNULL(1)
static inline int fev_waiters_queue_wait(struct fev_waiters_queue *queue,
                                         const struct timespec *abs_time,
                                         bool (*recheck)(void *arg), void *recheck_arg)
{
  struct fev_waiters_queue_node node;
  struct fev_sched_worker *cur_worker;
  struct fev_fiber *cur_fiber;
  struct fev_waiter *waiter;
  int res;

  /*
   * fev_waiters_queue_wait() should be called only within a fiber, thus both current worker and
   * fiber should not be NULL.
   */
  cur_worker = fev_cur_sched_worker;
  FEV_ASSERT(cur_worker != NULL);
  cur_fiber = cur_worker->cur_fiber;
  FEV_ASSERT(cur_fiber != NULL);

  /*
   * Prepare waiter. Stores here can be relaxed, as fev_ilock_unlock_and_wake() will issue a release
   * barrier, and the waiter won't be accessed outside of that critical section.
   */
  waiter = &node.waiter;
  atomic_store_explicit(&waiter->reason, FEV_WAITER_NONE, memory_order_relaxed);
  atomic_store_explicit(&waiter->do_wake, 0, memory_order_relaxed);
  atomic_store_explicit(&waiter->wait_for_wake, 1, memory_order_relaxed);
  waiter->fiber = cur_fiber;

  fev_ilock_lock(&queue->lock);

  if (recheck != NULL) {
    bool do_wait = recheck(recheck_arg);
    if (!do_wait) {
      fev_ilock_unlock_and_wake(&queue->lock);
      return 0;
    }
  }

  TAILQ_INSERT_TAIL(&queue->nodes, &node, tq_entry);
  node.deleted = false;

  fev_ilock_unlock_and_wake(&queue->lock);

  /* Wait. */
  if (abs_time == NULL) {
    enum fev_waiter_wake_reason reason;

    reason = fev_waiter_wait(waiter);
    (void)reason;

    FEV_ASSERT(reason == FEV_WAITER_READY);
  } else {
    res = fev_timed_wait(waiter, abs_time);
    if (res != 0)
      goto not_ready;
  }

  /* The node should have been removed by fev_waiters_queue_wake(). */
  FEV_ASSERT(node.deleted);

  return 0;

not_ready:
  FEV_ASSERT(res == -EAGAIN || res == -ENOMEM || res == -ETIMEDOUT);

  /* Remove the node if necessary. */
  fev_ilock_lock(&queue->lock);
  if (!node.deleted)
    TAILQ_REMOVE(&queue->nodes, &node, tq_entry);
  fev_ilock_unlock_and_wake(&queue->lock);

  return res;
}

/*
 * Wakes at most `max_waiters` that are waiting in `queue`. If `callback` is not null, it will be
 * called with `callback_arg`, the number of woken waiters and a flag whether the waiters queue is
 * empty now.
 */
FEV_NONNULL(1)
static inline void fev_waiters_queue_wake(struct fev_waiters_queue *queue, uint32_t max_waiters,
                                          void (*callback)(void *arg, uint32_t num_fibers,
                                                           bool is_empty),
                                          void *callback_arg)
{
  /* Fibers that we have to wake up. */
  fev_fiber_stq_head_t fibers = STAILQ_HEAD_INITIALIZER(fibers);
  uint32_t num_fibers = 0;

  /*
   * Number of waiters that are going to be woken up in result of this call. This includes the
   * waiters that we have to wake up (i.e. the list above) and the waiters that will be woken up by
   * fev_waiter_enable_wake_ups() (this happens when we manage to set the wake up reason, but not
   * `do_wake` flag).
   */
  uint32_t num_woken = 0;

  struct fev_fiber *fiber;

  fev_ilock_lock(&queue->lock);

  while (num_woken < max_waiters) {
    struct fev_waiter *waiter;
    enum fev_waiter_wake_result result;
    struct fev_waiters_queue_node *node;

    node = TAILQ_FIRST(&queue->nodes);

    if (node == NULL)
      break;

    TAILQ_REMOVE(&queue->nodes, node, tq_entry);
    node->deleted = true;

    waiter = &node->waiter;

    /* Try to wake up the waiter. */
    result = fev_waiter_wake(waiter, FEV_WAITER_READY);
    if (result != FEV_WAITER_FAILED) {
      FEV_ASSERT(result == FEV_WAITER_SET_ONLY || result == FEV_WAITER_SET_AND_WAKE_UP);

      /* We successfully set the wake reason of the waiter, and thus the waiter will be woken up. */
      num_woken++;

      /*
       * Do we have wake up the waiter? If this is false (`result` == FEV_WAITER_SET_ONLY), then
       * fev_waiter_enable_wake_ups() is going to wake up the waiter.
       */
      if (result == FEV_WAITER_SET_AND_WAKE_UP) {
        STAILQ_INSERT_TAIL(&fibers, waiter->fiber, stq_entry);
        num_fibers++;
      }
    }
  }

  if (callback != NULL) {
    bool is_empty = TAILQ_EMPTY(&queue->nodes);
    callback(callback_arg, num_woken, is_empty);
  }

  /*
   * Unlock the ilock and wake the fiber that is trying to access this waiters queue right now, if
   * any.
   */
  fiber = fev_ilock_unlock(&queue->lock);
  if (fiber != NULL) {
    STAILQ_INSERT_TAIL(&fibers, fiber, stq_entry);
    num_fibers++;
  }

  if (num_fibers > 0)
    fev_cur_wake_stq(&fibers, num_fibers);
}

#endif /* !FEV_WAITERS_QUEUE_IMPL_H */
