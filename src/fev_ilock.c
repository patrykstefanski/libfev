/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#include "fev_ilock_impl.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

#include <queue.h>

#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_context.h"
#include "fev_fiber.h"
#include "fev_sched_intf.h"

FEV_NONNULL(1) static void fev_ilock_lock_post(fev_ilock_lock_t *lock)
{
  struct fev_sched_worker *cur_worker;
  struct fev_sched *sched;

  cur_worker = fev_cur_sched_worker;
  FEV_ASSERT(cur_worker != NULL);

  sched = cur_worker->sched;
  FEV_ASSERT(sched != NULL);

  /* Decrease the ready counter, fev_wake_one/_stq will increase it. */
  atomic_fetch_sub_explicit(&sched->num_run_fibers, 1, memory_order_relaxed);

  fev_ilock_lock_unlock(lock);
}

FEV_NONNULL(1) bool fev_ilock_lock_slow(struct fev_ilock *ilock)
{
  struct fev_sched_worker *cur_worker;
  struct fev_fiber *cur_fiber;
  unsigned state;

  cur_worker = fev_cur_sched_worker;
  FEV_ASSERT(cur_worker != NULL);

  cur_fiber = cur_worker->cur_fiber;
  FEV_ASSERT(cur_fiber != NULL);

  fev_ilock_lock_lock(&ilock->lock);

  /* Update the state to 2 (locked, some waiters), as we are appending a waiter. */
  state = atomic_exchange_explicit(&ilock->state, 2, memory_order_relaxed);
  if (state == 0) {
    /*
     * The lock was unlocked inbetween the two swaps, update the state to 1 (locked, no waiters) and
     * return.
     */
    atomic_store_explicit(&ilock->state, 1, memory_order_relaxed);
    fev_ilock_lock_unlock(&ilock->lock);
    atomic_thread_fence(memory_order_acquire);
    return false;
  }

  /* Append the waiter. */
  STAILQ_INSERT_TAIL(&ilock->waiters, cur_fiber, stq_entry);

  /*
   * The spinlock must be unlocked after the context switch. Otherwise, a worker may be switching to
   * this fiber (after the ilock was unlocked and the waiter was woken up) while we are switching to
   * the scheduler at the same time.
   */
  fev_context_switch_and_call(&ilock->lock, &fev_ilock_lock_post, &cur_fiber->context,
                              &cur_worker->context);

  /* At this point we own the ilock. */
  FEV_ASSERT(atomic_load(&ilock->state) > 0);

  atomic_thread_fence(memory_order_acquire);
  return true;
}

FEV_NONNULL(1)
FEV_RETURNS_NONNULL FEV_WARN_UNUSED_RESULT struct fev_fiber *
fev_ilock_unlock_slow(struct fev_ilock *ilock)
{
  struct fev_fiber *fiber;

  fev_ilock_lock_lock(&ilock->lock);

  /*
   * At this point the state must be 2 (locked, some waiters):
   * 1. The ilock must be locked, thus the state must be at least 1.
   * 2. We have failed to swap from 1 to 0 in the fast path.
   * 3. The lock function cannot update the state from 2 to 1.
   */
  FEV_ASSERT(atomic_load(&ilock->state) == 2);

  /* Get the first waiting fiber. */
  fiber = STAILQ_FIRST(&ilock->waiters);
  FEV_ASSERT(fiber != NULL);

  /* Remove the queue's head. */
  if ((ilock->waiters.stqh_first = fiber->stq_entry.stqe_next) == NULL) {
    /* The queue is empty. */
    ilock->waiters.stqh_last = &ilock->waiters.stqh_first;

    /* No waiters, update the state to 1 (locked, no waiters). */
    atomic_store_explicit(&ilock->state, 1, memory_order_relaxed);
  }

  fev_ilock_lock_unlock(&ilock->lock);

  return fiber;
}
