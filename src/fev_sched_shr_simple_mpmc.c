/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#include "fev_sched_impl.h"

#include <errno.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "fev_alloc.h"
#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_context.h"
#include "fev_fiber.h"
#include "fev_poller.h"
#include "fev_simple_mpmc_pool.h"
#include "fev_simple_mpmc_queue.h"
#include "fev_thr_sem.h"

_Thread_local struct fev_sched_worker *fev_cur_sched_worker;

FEV_COLD FEV_NOINLINE FEV_NORETURN void fev_sched_oom(void)
{
  fputs("Failed to allocate memory to schedule a fiber\n", stderr);
  abort();
}

FEV_ALIGNED(FEV_ICACHE_LINE_SIZE)
FEV_HOT FEV_NOINLINE FEV_NONNULL(1) void fev_sched_work(struct fev_sched_worker *cur_worker)
{
  struct fev_sched *sched = cur_worker->sched;
  struct fev_fiber *cur_fiber;
  uint32_t backoff, num_run_fibers;

  goto get_fiber;

switch_to_fiber:
  cur_worker->cur_fiber = cur_fiber;
  fev_context_switch(&cur_worker->context, &cur_fiber->context);

  backoff = atomic_fetch_sub_explicit(&sched->poller_backoff, 1, memory_order_relaxed);
  if (FEV_UNLIKELY(backoff == 1))
    goto check_poller;

get_fiber:;
  struct fev_simple_mpmc_queue_node *node;
  bool popped = fev_simple_mpmc_queue_pop(sched->run_queue, (void **)&cur_fiber, &node);
  if (FEV_LIKELY(popped)) {
    fev_simple_mpmc_pool_free_local(&cur_worker->pool_local, node);
    goto switch_to_fiber;
  }

check_poller:
  fev_poller_check(cur_worker);

  num_run_fibers = atomic_load_explicit(&sched->num_run_fibers, memory_order_relaxed);
  atomic_store_explicit(&sched->poller_backoff, num_run_fibers, memory_order_relaxed);

  if (num_run_fibers > 0)
    goto get_fiber;

  /* Are we done? */
  if (FEV_UNLIKELY(atomic_load(&sched->num_fibers) == 0))
    goto out;

  /* Wait. */

  atomic_fetch_add(&sched->num_waiting, 1);

#ifdef FEV_POLLER_IO_URING
  fev_poller_wait(cur_worker);
#else
  bool poller_waiting = atomic_exchange(&sched->poller_waiting, true);
  if (!poller_waiting) {
    fev_poller_wait(cur_worker);
    atomic_store_explicit(&sched->poller_waiting, false, memory_order_relaxed);
  } else {
    fev_poller_quiescent(cur_worker);
    fev_thr_sem_wait(&sched->sem);
  }
#endif

  atomic_fetch_sub_explicit(&sched->num_waiting, 1, memory_order_relaxed);

  goto get_fiber;

out:
  fev_sched_wake_all_workers(sched);
}

FEV_COLD FEV_NONNULL(1, 2) int fev_sched_put(struct fev_sched *sched, struct fev_fiber *fiber)
{
  struct fev_simple_mpmc_queue_node *node;

  node = fev_simple_mpmc_pool_alloc_global(&sched->pool_global);
  if (FEV_UNLIKELY(node == NULL))
    return -ENOMEM;

  fev_simple_mpmc_queue_push(sched->run_queue, node, fiber);
  atomic_fetch_add_explicit(&sched->num_run_fibers, 1, memory_order_relaxed);
  return 0;
}

FEV_COLD FEV_NONNULL(1) static int fev_sched_init_workers(struct fev_sched *sched,
                                                          uint32_t num_workers)
{
  struct fev_sched_worker *workers;

  FEV_ASSERT(num_workers > 0);

  workers = fev_aligned_alloc(alignof(struct fev_sched_worker), num_workers * sizeof(*workers));
  if (FEV_UNLIKELY(workers == NULL))
    return -ENOMEM;

  for (uint32_t i = 0; i < num_workers; i++) {
    struct fev_sched_worker *worker = &workers[i];
    worker->sched = sched;
    worker->run_queue = sched->run_queue;
    fev_simple_mpmc_pool_init_local(&worker->pool_local, &sched->pool_global,
                                    FEV_SCHED_SHR_SIMPLE_MPMC_LOCAL_POOL_SIZE);
  }

  sched->workers = workers;
  sched->num_workers = num_workers;
  return 0;
}

FEV_COLD FEV_NONNULL(1) static void fev_sched_fini_workers(struct fev_sched *sched)
{
  for (uint32_t i = 0; i < sched->num_workers; i++)
    fev_simple_mpmc_pool_fini_local(&sched->workers[i].pool_local);

  fev_aligned_free(sched->workers);
}

FEV_COLD FEV_NONNULL(1) int fev_sched_init(struct fev_sched *sched, uint32_t num_workers)
{
  struct fev_simple_mpmc_queue_node *node;
  int ret = -ENOMEM;

  sched->run_queue =
      fev_aligned_alloc(alignof(struct fev_simple_mpmc_queue), sizeof(*sched->run_queue));
  if (FEV_UNLIKELY(sched->run_queue == NULL))
    goto fail;

  fev_simple_mpmc_pool_init_global(&sched->pool_global);

  node = fev_simple_mpmc_pool_alloc_global(&sched->pool_global);
  if (FEV_UNLIKELY(node == NULL))
    goto fail_pool;

  fev_simple_mpmc_queue_init(sched->run_queue, node);

  /* Must be after initialization of run queue. */
  ret = fev_sched_init_workers(sched, num_workers);
  if (FEV_UNLIKELY(ret != 0))
    goto fail_node;

  /* Must be after initialization of workers. */
  ret = fev_poller_init(sched);
  if (FEV_UNLIKELY(ret != 0))
    goto fail_workers;

  ret = fev_timers_init(&sched->timers);
  if (FEV_UNLIKELY(ret != 0))
    goto fail_poller;

  ret = fev_thr_sem_init(&sched->sem, 0);
  if (FEV_UNLIKELY(ret != 0))
    goto fail_timers;

  atomic_init(&sched->poller_backoff, 1);
  atomic_init(&sched->num_waiting, 0);
  atomic_init(&sched->poller_waiting, false);
  atomic_init(&sched->num_run_fibers, 0);
  atomic_init(&sched->num_fibers, 0);

  sched->start_sem = NULL;

  return 0;

fail_timers:
  fev_timers_fini(&sched->timers);

fail_poller:
  fev_poller_fini(sched);

fail_workers:
  fev_sched_fini_workers(sched);

fail_node:
  fev_simple_mpmc_queue_fini(sched->run_queue, &node);
  fev_simple_mpmc_pool_free_global(&sched->pool_global, node);

fail_pool:
  fev_simple_mpmc_pool_fini_global(&sched->pool_global);
  fev_aligned_free(sched->run_queue);

fail:
  return ret;
}

FEV_COLD FEV_NONNULL(1) void fev_sched_fini(struct fev_sched *sched)
{
  struct fev_simple_mpmc_queue_node *node;

  fev_thr_sem_fini(&sched->sem);
  fev_timers_fini(&sched->timers);
  fev_poller_fini(sched);
  fev_sched_fini_workers(sched);
  fev_simple_mpmc_queue_fini(sched->run_queue, &node);
  fev_simple_mpmc_pool_free_global(&sched->pool_global, node);
  fev_simple_mpmc_pool_fini_global(&sched->pool_global);
  fev_aligned_free(sched->run_queue);
}
