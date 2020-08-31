/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_SCHED_SHR_LOCKING_IMPL_H
#define FEV_SCHED_SHR_LOCKING_IMPL_H

#include "fev_sched_intf.h"

#include <stdint.h>

#include <queue.h>

#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_fiber.h"
#include "fev_thr_mutex.h"

FEV_NONNULL(1, 2)
static inline void fev_push_one(struct fev_sched_worker *worker, struct fev_fiber *fiber)
{
  struct fev_sched *sched = worker->sched;

  fev_thr_mutex_lock(&sched->run_queue_lock);
  STAILQ_INSERT_TAIL(&sched->run_queue, fiber, stq_entry);
  fev_thr_mutex_unlock(&sched->run_queue_lock);
}

FEV_NONNULL(1, 2)
static inline void fev_push_stq(struct fev_sched_worker *worker, fev_fiber_stq_head_t *fibers,
                                uint32_t num_fibers)
{
  struct fev_sched *sched = worker->sched;
  fev_fiber_stq_head_t *run_queue = &sched->run_queue;
  struct fev_fiber *first = fibers->stqh_first, **last = fibers->stqh_last;

  (void)num_fibers;

  FEV_ASSERT(!STAILQ_EMPTY(fibers));
  FEV_ASSERT(num_fibers > 0);

  fev_thr_mutex_lock(&sched->run_queue_lock);
  *run_queue->stqh_last = first;
  run_queue->stqh_last = last;
  fev_thr_mutex_unlock(&sched->run_queue_lock);
}

#endif /* !FEV_SCHED_SHR_LOCKING_IMPL_H */
