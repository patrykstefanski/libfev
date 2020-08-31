/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_SCHED_STEAL_LOCKING_IMPL_H
#define FEV_SCHED_STEAL_LOCKING_IMPL_H

#include "fev_sched_intf.h"

#include <stdatomic.h>
#include <stdint.h>

#include <queue.h>

#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_fiber.h"
#include "fev_spinlock_impl.h"
#include "fev_thr_mutex.h"

#if defined(FEV_SCHED_STEAL_LOCKING_LOCK_MUTEX)
#define fev_sched_run_queue_lock_init fev_thr_mutex_init
#define fev_sched_run_queue_lock_fini fev_thr_mutex_fini
#define fev_sched_run_queue_lock fev_thr_mutex_lock
#define fev_sched_run_queue_try_lock fev_thr_mutex_try_lock
#define fev_sched_run_queue_unlock fev_thr_mutex_unlock
#elif defined(FEV_SCHED_STEAL_LOCKING_LOCK_SPINLOCK)
#define fev_sched_run_queue_lock_init fev_spinlock_init
#define fev_sched_run_queue_lock_fini fev_spinlock_fini
#define fev_sched_run_queue_lock fev_spinlock_lock
#define fev_sched_run_queue_try_lock fev_spinlock_try_lock
#define fev_sched_run_queue_unlock fev_spinlock_unlock
#endif

FEV_NONNULL(1, 2)
static inline void fev_push_one(struct fev_sched_worker *worker, struct fev_fiber *fiber)
{
  struct fev_sched_run_queue *run_queue = &worker->run_queue;

  fev_sched_run_queue_lock(&run_queue->lock);

  STAILQ_INSERT_TAIL(&run_queue->head, fiber, stq_entry);
  atomic_fetch_add_explicit(&run_queue->size, 1, memory_order_relaxed);

  fev_sched_run_queue_unlock(&run_queue->lock);
}

FEV_NONNULL(1, 2)
static inline void fev_push_stq(struct fev_sched_worker *worker, fev_fiber_stq_head_t *fibers,
                                uint32_t num_fibers)
{
  struct fev_sched_run_queue *run_queue = &worker->run_queue;
  struct fev_fiber *first = fibers->stqh_first, **last = fibers->stqh_last;

  FEV_ASSERT(!STAILQ_EMPTY(fibers));
  FEV_ASSERT(num_fibers > 0);

  fev_sched_run_queue_lock(&run_queue->lock);

  *run_queue->head.stqh_last = first;
  run_queue->head.stqh_last = last;

  atomic_fetch_add_explicit(&run_queue->size, num_fibers, memory_order_relaxed);

  fev_sched_run_queue_unlock(&run_queue->lock);
}

#endif /* !FEV_SCHED_STEAL_LOCKING_IMPL_H */
