/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_SCHED_SHR_SIMPLE_MPMC_IMPL_H
#define FEV_SCHED_SHR_SIMPLE_MPMC_IMPL_H

#include "fev_sched_intf.h"

#include <stddef.h>
#include <stdint.h>

#include <queue.h>

#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_fiber.h"
#include "fev_simple_mpmc_pool.h"
#include "fev_simple_mpmc_queue.h"

FEV_NONNULL(1, 2)
static inline void fev_push_one(struct fev_sched_worker *worker, struct fev_fiber *fiber)
{
  struct fev_simple_mpmc_queue_node *node;

  node = fev_simple_mpmc_pool_alloc_local(&worker->pool_local);
  if (FEV_UNLIKELY(node == NULL))
    fev_sched_oom();

  fev_simple_mpmc_queue_push(worker->run_queue, node, fiber);
}

FEV_NONNULL(1, 2)
static inline void fev_push_stq(struct fev_sched_worker *worker, fev_fiber_stq_head_t *fibers,
                                uint32_t num_fibers)
{
  struct fev_simple_mpmc_queue *run_queue = worker->run_queue;
  struct fev_simple_mpmc_queue_node *node;
  struct fev_fiber *cur;

  (void)num_fibers;

  FEV_ASSERT(!STAILQ_EMPTY(fibers));
  FEV_ASSERT(num_fibers > 0);

  cur = STAILQ_FIRST(fibers);
  while (cur != NULL) {
    struct fev_fiber *next = STAILQ_NEXT(cur, stq_entry);

    node = fev_simple_mpmc_pool_alloc_local(&worker->pool_local);
    if (FEV_UNLIKELY(node == NULL))
      fev_sched_oom();

    fev_simple_mpmc_queue_push(run_queue, node, cur);

    cur = next;
  }
}

#endif /* !FEV_SCHED_SHR_SIMPLE_MPMC_IMPL_H */
