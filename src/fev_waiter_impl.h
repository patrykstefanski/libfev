/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_WAITER_IMPL_H
#define FEV_WAITER_IMPL_H

#include "fev_waiter_intf.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_context.h"
#include "fev_fiber.h"
#include "fev_sched_impl.h"

/* Allows the fiber in the passed waiter to be woken up. */
FEV_NONNULL(1) static inline void fev_waiter_enable_wake_ups(struct fev_waiter *waiter)
{
  struct fev_fiber *fiber = NULL;
  unsigned reason;

  /*
   * We are in the scheduler context (worker thread). The fiber's context is saved and therefore we
   * can now allow wake ups (they will restore the context). We synchronize with the store to
   * `wait_for_post` in fev_waiter_wait(), so that the worker that will wake up the fiber won't see
   * trash. Secondly, we need to make sure that the following load is not reordered before this
   * store. Otherwise, the fiber could be not woken up.
   */
  atomic_store_explicit(&waiter->do_wake, 1, memory_order_seq_cst);

  /*
   * Some worker may have already updated `reason` before the previous store, but failed to update
   * the `do_wake`. Thus, we must check it again. The load can be relaxed, since the following
   * writes have the release semantics.
   */
  reason = atomic_load_explicit(&waiter->reason, memory_order_relaxed);
  if (reason != 0) {
    unsigned do_wake;

    /*
     * Try to wake up the fiber. We may compete with another worker that is trying to exchange
     * `do_wake` right now.
     */
    do_wake = atomic_exchange_explicit(&waiter->do_wake, 0, memory_order_acq_rel);
    if (do_wake) {
      atomic_store_explicit(&waiter->wake_reason, reason, memory_order_relaxed);

      fiber = waiter->fiber;
    }
  }

  /*
   * The fiber can be woken up just after the store to `do_wake`. Thus, we need to make sure that
   * the woken up fiber won't return and cause a stack-use-after-return bug, since the waiter is
   * accessed after that store to `do_wake` here. To do that, the fiber will spin until
   * `wait_for_post` is 0. The release barrier ensures that no access above is reordered after the
   * store to `wait_for_post` here.
   */
  atomic_store_explicit(&waiter->wait_for_post, 0, memory_order_release);

  if (fiber != NULL)
    fev_cur_wake_one(fiber);
}

/* Waits on a waiter, returns the reason of a wake up. */
FEV_NONNULL(1) static inline unsigned fev_waiter_wait(struct fev_waiter *waiter)
{
  struct fev_sched_worker *cur_worker;
  struct fev_sched *sched;
  struct fev_fiber *fiber;

  /* This should be set by the caller. */
  FEV_ASSERT(atomic_load(&waiter->do_wake) == 0);

  fiber = waiter->fiber;
  FEV_ASSERT(fiber != NULL);

  cur_worker = fev_cur_sched_worker;
  FEV_ASSERT(cur_worker != NULL);

  sched = cur_worker->sched;
  FEV_ASSERT(sched != NULL);

  atomic_fetch_sub_explicit(&sched->num_run_fibers, 1, memory_order_relaxed);

  /*
   * This store must happen before updating `do_wake` in the post operation. Otherwise, the fiber
   * can be woken up and see `wait_for_post` as 0, and then leave the following loop and
   * therefore overwrite the content of the waiter causing stack-use-after-return.
   */
  atomic_store_explicit(&waiter->wait_for_post, 1, memory_order_relaxed);

  fev_context_switch_and_call(waiter, &fev_waiter_enable_wake_ups, &fiber->context,
                              &cur_worker->context);

  /* Spin until fev_waiter_enable_wake_ups() and fev_waiter_wake() are done. */
  while (FEV_UNLIKELY(atomic_load_explicit(&waiter->wait, memory_order_acquire) != 0)) {
    atomic_fetch_sub_explicit(&sched->num_run_fibers, 1, memory_order_relaxed);

    /*
     * Reload the current worker, since we switched to the scheduler and the fiber may be scheduled
     * on a different worker.
     */
    cur_worker = fev_cur_sched_worker;
    fev_context_switch_and_call(fiber, &fev_cur_wake_one, &fiber->context, &cur_worker->context);
  }

  return atomic_load_explicit(&waiter->wake_reason, memory_order_acquire);
}

FEV_NONNULL(1)
static inline enum fev_waiter_wake_result fev_waiter_wake(struct fev_waiter *waiter,
                                                          enum fev_waiter_wake_reason reason)
{
  unsigned expected;
  bool success;

  /* The caller should not pass FEV_WAITER_NONE. */
  FEV_ASSERT(reason != FEV_WAITER_NONE);

  /* Assure that the waiter's state is valid. */
  FEV_ASSERT(atomic_load(&waiter->reason) <= FEV_WAITER_TIMED_OUT_NO_CHECK);
  FEV_ASSERT(atomic_load(&waiter->do_wake) <= 1);

  expected = FEV_WAITER_NONE;
  success = atomic_compare_exchange_strong_explicit(&waiter->reason, &expected, reason,
                                                    memory_order_relaxed, memory_order_relaxed);
  if (FEV_LIKELY(success)) {
    enum fev_waiter_wake_result result;
    unsigned do_wake;

    /* We have set the reason, try to wake up then. */
    do_wake = atomic_exchange_explicit(&waiter->do_wake, 0, memory_order_acq_rel);
    if (FEV_LIKELY(do_wake != 0)) {
      atomic_store_explicit(&waiter->wake_reason, reason, memory_order_relaxed);

      result = FEV_WAITER_SET_AND_WAKE_UP;
    } else {
      result = FEV_WAITER_SET_ONLY;
    }

    atomic_store_explicit(&waiter->wait_for_wake, 0, memory_order_release);
    return result;
  }

  return FEV_WAITER_FAILED;
}

#endif /* !FEV_WAITER_IMPL_H */
