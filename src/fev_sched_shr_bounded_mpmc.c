/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#include "fev_sched_impl.h"

#include <assert.h>
#include <errno.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <queue.h>

#include "fev_alloc.h"
#include "fev_assert.h"
#include "fev_bounded_mpmc_queue.h"
#include "fev_compiler.h"
#include "fev_context.h"
#include "fev_fiber.h"
#include "fev_poller.h"
#include "fev_thr_mutex.h"
#include "fev_thr_sem.h"

static_assert((FEV_SCHED_SHR_BOUNDED_MPMC_QUEUE_CAPACITY &
               (FEV_SCHED_SHR_BOUNDED_MPMC_QUEUE_CAPACITY - 1)) == 0,
              "Queue capacity must be a power of 2");

_Thread_local struct fev_sched_worker *fev_cur_sched_worker;

FEV_NONNULL(1) static void fev_move_from_fallback(struct fev_sched *sched)
{
  uint32_t len, n;

  fev_thr_mutex_lock(&sched->fallback_queue_lock);
  len = atomic_load_explicit(&sched->fallback_queue_len, memory_order_relaxed);
  if (len > 0) {
    n = len;
    fev_bounded_mpmc_queue_push_stq(&sched->run_queue, &sched->fallback_queue, &n);
    FEV_ASSERT(n <= len);
    atomic_store_explicit(&sched->fallback_queue_len, len - n, memory_order_relaxed);
  }
  fev_thr_mutex_unlock(&sched->fallback_queue_lock);
}

FEV_NONNULL(1, 2) void fev_push_one_fallback(struct fev_sched *sched, struct fev_fiber *fiber)
{
  fev_thr_mutex_lock(&sched->fallback_queue_lock);
  STAILQ_INSERT_TAIL(&sched->fallback_queue, fiber, stq_entry);
  atomic_fetch_add_explicit(&sched->fallback_queue_len, 1, memory_order_relaxed);
  fev_thr_mutex_unlock(&sched->fallback_queue_lock);
}

FEV_NONNULL(1, 2)
void fev_push_stq_fallback(struct fev_sched *sched, fev_fiber_stq_head_t *fibers,
                           uint32_t num_fibers)
{
  fev_fiber_stq_head_t *fallback_queue = &sched->fallback_queue;

  FEV_ASSERT(!STAILQ_EMPTY(fibers));
  FEV_ASSERT(num_fibers > 0);

  fev_thr_mutex_lock(&sched->fallback_queue_lock);
  *fallback_queue->stqh_last = fibers->stqh_first;
  fallback_queue->stqh_last = fibers->stqh_last;
  atomic_fetch_add_explicit(&sched->fallback_queue_len, num_fibers, memory_order_relaxed);
  fev_thr_mutex_unlock(&sched->fallback_queue_lock);
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
  bool popped = fev_bounded_mpmc_queue_pop(&sched->run_queue, (void **)&cur_fiber);
  if (popped)
    goto switch_to_fiber;

check_poller:
  fev_poller_check(cur_worker);

  if (atomic_load_explicit(&sched->fallback_queue_len, memory_order_relaxed) > 0)
    fev_move_from_fallback(sched);

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
  /* 'sched' should be initialized and thus 'num_workers' should be positive. */
  FEV_ASSERT(sched->num_workers > 0);

  fev_push_one(&sched->workers[0], fiber);

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

  ret = fev_bounded_mpmc_queue_init(&sched->run_queue, FEV_SCHED_SHR_BOUNDED_MPMC_QUEUE_CAPACITY);
  if (FEV_UNLIKELY(ret != 0))
    goto fail_sem;

  ret = fev_thr_mutex_init(&sched->fallback_queue_lock);
  if (FEV_UNLIKELY(ret != 0))
    goto fail_bounded_mpmc_queue;

  STAILQ_INIT(&sched->fallback_queue);
  atomic_init(&sched->fallback_queue_len, 0);

  atomic_init(&sched->poller_backoff, 1);
  atomic_init(&sched->num_waiting, 0);
  atomic_init(&sched->poller_waiting, false);
  atomic_init(&sched->num_run_fibers, 0);
  atomic_init(&sched->num_fibers, 0);

  sched->start_sem = NULL;

  return 0;

fail_bounded_mpmc_queue:
  fev_bounded_mpmc_queue_fini(&sched->run_queue);

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
  fev_thr_mutex_fini(&sched->fallback_queue_lock);
  fev_bounded_mpmc_queue_fini(&sched->run_queue);
  fev_thr_sem_fini(&sched->sem);
  fev_timers_fini(&sched->timers);
  fev_poller_fini(sched);
  fev_aligned_free(sched->workers);
}
