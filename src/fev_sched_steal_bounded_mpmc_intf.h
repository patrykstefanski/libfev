/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_SCHED_STEAL_BOUNDED_MPMC_INTF_H
#define FEV_SCHED_STEAL_BOUNDED_MPMC_INTF_H

#include <stdalign.h>
#include <stdatomic.h>
#include <stdint.h>

#include "fev_bounded_mpmc_queue.h"
#include "fev_compiler.h"
#include "fev_context.h"
#include "fev_fiber.h"
#include "fev_poller.h"
#include "fev_thr_mutex.h"
#include "fev_thr_sem.h"
#include "fev_timers.h"

struct fev_sched_worker {
  alignas(FEV_DCACHE_LINE_SIZE) struct fev_bounded_mpmc_queue run_queue;

  alignas(FEV_DCACHE_LINE_SIZE) struct fev_fiber *cur_fiber;
  struct fev_context context;
  struct fev_worker_poller_data poller_data;
  struct fev_sched *sched;
  uint32_t rnd;
};

struct fev_sched {
  /* Number of waiting workers. */
  _Atomic uint32_t num_waiting;

  /* Is any worker waiting on poller? */
  atomic_bool poller_waiting;

  /* Number of runnable fibers. */
  _Atomic uint32_t num_run_fibers;

  /* Total number of fibers (runnable & blocked). */
  _Atomic uint32_t num_fibers;

  struct fev_thr_mutex fallback_queue_lock;
  fev_fiber_stq_head_t fallback_queue;
  _Atomic uint32_t fallback_queue_len;

  struct fev_poller poller;
  struct fev_timers timers;

  struct fev_thr_sem sem;

  struct fev_sched_worker *workers;
  uint32_t num_workers;

  struct fev_thr_sem *start_sem;
};

FEV_NONNULL(1, 2)
void fev_push_one_fallback(struct fev_sched_worker *worker, struct fev_fiber *fiber);

FEV_NONNULL(1, 2)
void fev_push_stq_fallback(struct fev_sched_worker *worker, fev_fiber_stq_head_t *fibers,
                           uint32_t num_fibers);

#endif /* !FEV_SCHED_STEAL_BOUNDED_MPMC_INTF_H */
