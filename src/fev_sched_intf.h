/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_SCHED_INTF_H
#define FEV_SCHED_INTF_H

#include <fev/fev.h>

#include <stdint.h>

#include "fev_compiler.h"

#if defined(FEV_SCHED_WORK_SHARING_LOCKING)
#include "fev_sched_shr_locking_intf.h"
#elif defined(FEV_SCHED_WORK_SHARING_BOUNDED_MPMC)
#include "fev_sched_shr_bounded_mpmc_intf.h"
#elif defined(FEV_SCHED_WORK_SHARING_SIMPLE_MPMC)
#include "fev_sched_shr_simple_mpmc_intf.h"
#elif defined(FEV_SCHED_WORK_STEALING_LOCKING)
#include "fev_sched_steal_locking_intf.h"
#elif defined(FEV_SCHED_WORK_STEALING_BOUNDED_MPMC)
#include "fev_sched_steal_bounded_mpmc_intf.h"
#elif defined(FEV_SCHED_WORK_STEALING_BOUNDED_SPMC)
#include "fev_sched_steal_bounded_spmc_intf.h"
#else
#error Wrong scheduler
#endif

extern _Thread_local struct fev_sched_worker *fev_cur_sched_worker;

FEV_NONNULL(1)
void fev_wake_workers_slow(struct fev_sched_worker *worker, uint32_t num_waiting,
                           uint32_t num_fibers);

FEV_NONNULL(1) void fev_sched_wake_all_workers(struct fev_sched *sched);

FEV_NONNULL(1, 2) int fev_sched_put(struct fev_sched *sched, struct fev_fiber *fiber);

FEV_NONNULL(1) void fev_sched_work(struct fev_sched_worker *cur_worker);

FEV_NONNULL(1) int fev_sched_init(struct fev_sched *sched, uint32_t num_workers);

FEV_NONNULL(1) void fev_sched_fini(struct fev_sched *sched);

#endif /* !FEV_SCHED_INTF_H */
