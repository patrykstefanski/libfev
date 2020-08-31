/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_SCHED_IMPL_H
#define FEV_SCHED_IMPL_H

#include "fev_sched_intf.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fev_compiler.h"
#include "fev_fiber.h"

#if defined(FEV_SCHED_WORK_SHARING_LOCKING)
#include "fev_sched_shr_locking_impl.h"
#elif defined(FEV_SCHED_WORK_SHARING_SIMPLE_MPMC)
#include "fev_sched_shr_simple_mpmc_impl.h"
#elif defined(FEV_SCHED_WORK_SHARING_BOUNDED_MPMC)
#include "fev_sched_shr_bounded_mpmc_impl.h"
#elif defined(FEV_SCHED_WORK_STEALING_LOCKING)
#include "fev_sched_steal_locking_impl.h"
#elif defined(FEV_SCHED_WORK_STEALING_BOUNDED_MPMC)
#include "fev_sched_steal_bounded_mpmc_impl.h"
#elif defined(FEV_SCHED_WORK_STEALING_BOUNDED_SPMC)
#include "fev_sched_steal_bounded_spmc_impl.h"
#endif

FEV_NONNULL(1)
static inline void fev_wake_up_waiting_workers(struct fev_sched_worker *worker, uint32_t num_fibers)
{
  struct fev_sched *sched = worker->sched;

  atomic_fetch_add(&sched->num_run_fibers, num_fibers);

  /* Get the number of waiting workers. */
  uint32_t num_waiting = atomic_load(&sched->num_waiting);
  if (FEV_LIKELY(num_waiting == 0)) {
    /* No worker is waiting, we don't have to do anything. */
    return;
  }

  fev_wake_workers_slow(worker, num_waiting, num_fibers);
}

FEV_NONNULL(1, 2)
static inline void fev_wake_one(struct fev_sched_worker *worker, struct fev_fiber *fiber)
{
  fev_push_one(worker, fiber);
  fev_wake_up_waiting_workers(worker, /*num_fibers=*/1);
}

FEV_NONNULL(1, 2)
static inline void fev_wake_stq(struct fev_sched_worker *worker, fev_fiber_stq_head_t *fibers,
                                uint32_t num_fibers)
{
  fev_push_stq(worker, fibers, num_fibers);
  fev_wake_up_waiting_workers(worker, num_fibers);
}

FEV_NONNULL(1) static inline void fev_cur_wake_one(struct fev_fiber *fiber)
{
  fev_wake_one(fev_cur_sched_worker, fiber);
}

FEV_NONNULL(1)
static inline void fev_cur_wake_stq(fev_fiber_stq_head_t *fibers, uint32_t num_fibers)
{
  fev_wake_stq(fev_cur_sched_worker, fibers, num_fibers);
}

FEV_NONNULL(1) static inline bool fev_sched_is_running(struct fev_sched *sched)
{
  return sched->start_sem != NULL;
}

static inline struct fev_fiber *fev_cur_fiber(void) { return fev_cur_sched_worker->cur_fiber; }

#endif /* !FEV_SCHED_IMPL_H */
