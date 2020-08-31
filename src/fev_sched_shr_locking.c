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
#include <stddef.h>
#include <stdint.h>

#include <queue.h>

#include "fev_alloc.h"
#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_context.h"
#include "fev_fiber.h"
#include "fev_poller.h"
#include "fev_thr_mutex.h"
#include "fev_thr_sem.h"

_Thread_local struct fev_sched_worker *fev_cur_sched_worker;

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

get_fiber:
  fev_thr_mutex_lock(&sched->run_queue_lock);
  cur_fiber = STAILQ_FIRST(&sched->run_queue);
  if (FEV_LIKELY(cur_fiber != NULL)) {
    STAILQ_REMOVE_HEAD(&sched->run_queue, stq_entry);
    fev_thr_mutex_unlock(&sched->run_queue_lock);
    goto switch_to_fiber;
  }
  fev_thr_mutex_unlock(&sched->run_queue_lock);

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
  STAILQ_INSERT_HEAD(&sched->run_queue, fiber, stq_entry);

  /* One runnable fiber was added. */
  atomic_fetch_add_explicit(&sched->num_run_fibers, 1, memory_order_relaxed);

  /*
   * This function should be used before calling fev_sched_run() and the workers should not be
   * running yet, thus we don't wake any worker here.
   */

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

  for (uint32_t i = 0; i < num_workers; i++)
    workers[i].sched = sched;

  sched->workers = workers;
  sched->num_workers = num_workers;
  return 0;
}

FEV_COLD FEV_NONNULL(1) int fev_sched_init(struct fev_sched *sched, uint32_t num_workers)
{
  int ret;

  ret = fev_sched_init_workers(sched, num_workers);
  if (FEV_UNLIKELY(ret != 0))
    goto fail;

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

  ret = fev_thr_mutex_init(&sched->run_queue_lock);
  if (FEV_UNLIKELY(ret != 0))
    goto fail_sem;

  STAILQ_INIT(&sched->run_queue);

  atomic_init(&sched->poller_backoff, 1);
  atomic_init(&sched->num_waiting, 0);
  atomic_init(&sched->poller_waiting, false);
  atomic_init(&sched->num_run_fibers, 0);
  atomic_init(&sched->num_fibers, 0);

  sched->start_sem = NULL;

  return 0;

fail_sem:
  fev_thr_sem_fini(&sched->sem);

fail_timers:
  fev_timers_fini(&sched->timers);

fail_poller:
  fev_poller_fini(sched);

fail_workers:
  fev_aligned_free(sched->workers);

fail:
  return ret;
}

FEV_COLD FEV_NONNULL(1) void fev_sched_fini(struct fev_sched *sched)
{
  fev_thr_mutex_fini(&sched->run_queue_lock);
  fev_thr_sem_fini(&sched->sem);
  fev_timers_fini(&sched->timers);
  fev_poller_fini(sched);
  fev_aligned_free(sched->workers);
}
