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
#include "fev_bounded_spmc_queue.h"
#include "fev_compiler.h"
#include "fev_context.h"
#include "fev_fiber.h"
#include "fev_poller.h"
#include "fev_thr_mutex.h"
#include "fev_thr_sem.h"
#include "fev_util.h"

static_assert((FEV_SCHED_STEAL_BOUNDED_SPMC_QUEUE_CAPACITY &
               (FEV_SCHED_STEAL_BOUNDED_SPMC_QUEUE_CAPACITY - 1)) == 0,
              "Queue capacity must be a power of 2");
static_assert(FEV_SCHED_STEAL_BOUNDED_SPMC_STEAL_COUNT <
                  FEV_SCHED_STEAL_BOUNDED_SPMC_QUEUE_CAPACITY,
              "Number of fibers to steal must be smaller than the queue capacity");

_Thread_local struct fev_sched_worker *fev_cur_sched_worker;

FEV_NONNULL(1)
static uint32_t fev_sched_steal(struct fev_sched_worker *worker)
{
  struct fev_sched *sched = worker->sched;
  struct fev_sched_worker *workers = sched->workers;
  uint32_t num_workers = sched->num_workers, rnd, num_stolen = 0;
  fev_fiber_stq_head_t fibers = STAILQ_HEAD_INITIALIZER(fibers);

  rnd = worker->rnd = FEV_RANDOM_NEXT(worker->rnd);

  for (uint32_t i = 0; i < num_workers; i++) {
    struct fev_sched_worker *victim;
    struct fev_bounded_spmc_queue *victim_rq;
    struct fev_fiber *fiber;

    victim = &workers[(rnd + i) % num_workers];

    /* Don't steal from ourselves, we don't have any fibers. */
    if (victim == worker)
      continue;

    victim_rq = &victim->run_queue;

    for (; num_stolen < FEV_SCHED_STEAL_BOUNDED_SPMC_STEAL_COUNT; num_stolen++) {
      bool popped = fev_bounded_spmc_queue_pop(victim_rq, (void **)&fiber);
      if (!popped)
        break;
      STAILQ_INSERT_TAIL(&fibers, fiber, stq_entry);
    }

    if (num_stolen > 0) {
      uint32_t n = num_stolen;
      fev_bounded_spmc_queue_push_stq(&worker->run_queue, &fibers, &n);

      /*
       * The queue was empty and since  FEV_SCHED_STEAL_BOUNDED_SPMC_STEAL_COUNT is smaller than
       * FEV_SCHED_STEAL_BOUNDED_SPMC_QUEUE_CAPACITY, then we should have pushed all of them.
       */
      FEV_ASSERT(n == num_stolen);

      break;
    }
  }

  return num_stolen;
}

FEV_NONNULL(1) static void fev_move_from_fallback(struct fev_sched_worker *worker)
{
  struct fev_sched *sched = worker->sched;
  uint32_t len, n;

  fev_thr_mutex_lock(&sched->fallback_queue_lock);
  len = atomic_load_explicit(&sched->fallback_queue_len, memory_order_relaxed);
  if (len > 0) {
    n = len;
    fev_bounded_spmc_queue_push_stq(&worker->run_queue, &sched->fallback_queue, &n);
    FEV_ASSERT(n <= len);
    atomic_store_explicit(&sched->fallback_queue_len, len - n, memory_order_relaxed);
  }
  fev_thr_mutex_unlock(&sched->fallback_queue_lock);
}

FEV_NONNULL(1, 2)
void fev_push_one_fallback(struct fev_sched_worker *worker, struct fev_fiber *fiber)
{
  struct fev_sched *sched = worker->sched;

  fev_thr_mutex_lock(&sched->fallback_queue_lock);
  STAILQ_INSERT_TAIL(&sched->fallback_queue, fiber, stq_entry);
  atomic_fetch_add_explicit(&sched->fallback_queue_len, 1, memory_order_relaxed);
  fev_thr_mutex_unlock(&sched->fallback_queue_lock);
}

FEV_NONNULL(1, 2)
void fev_push_stq_fallback(struct fev_sched_worker *worker, fev_fiber_stq_head_t *fibers,
                           uint32_t num_fibers)
{
  struct fev_sched *sched = worker->sched;
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
  struct fev_bounded_spmc_queue *run_queue = &cur_worker->run_queue;
  struct fev_fiber *cur_fiber;
  uint32_t backoff = 0;

  goto get_local;

switch_to_fiber:
  cur_worker->cur_fiber = cur_fiber;
  fev_context_switch(&cur_worker->context, &cur_fiber->context);

  if (FEV_UNLIKELY(backoff == 0))
    goto get_global;

  --backoff;

get_local:;
  bool popped = fev_bounded_spmc_queue_pop(run_queue, (void **)&cur_fiber);
  if (FEV_LIKELY(popped))
    goto switch_to_fiber;

get_global:
  fev_poller_check(cur_worker);

  if (atomic_load_explicit(&sched->fallback_queue_len, memory_order_relaxed) > 0)
    fev_move_from_fallback(cur_worker);

  backoff = fev_bounded_spmc_queue_size(run_queue);
  if (backoff > 0)
    goto get_local;

  uint32_t num_stolen = fev_sched_steal(cur_worker);
  if (num_stolen > 0) {
    backoff = num_stolen;
    goto get_local;
  }

  /* Check if the worker can go to sleep. */

  uint32_t num_run_fibers = atomic_load_explicit(&sched->num_run_fibers, memory_order_relaxed);
  uint32_t num_waiting = atomic_load_explicit(&sched->num_waiting, memory_order_relaxed);

  if (FEV_LIKELY(num_run_fibers >= sched->num_workers - num_waiting)) {
    /*
     * There are still some runnable fibers. Our local queue is however empty, try to get some
     * fibers from global state or steal some.
     */
    goto get_global;
  }

  if (FEV_UNLIKELY(num_run_fibers == 0)) {
    /* Are we done? */
    if (FEV_UNLIKELY(atomic_load(&sched->num_fibers) == 0))
      goto out;
  }

  /* Try to sleep. */

  bool poller_waiting = atomic_exchange(&sched->poller_waiting, true);

  num_waiting = atomic_fetch_add(&sched->num_waiting, 1);
  num_run_fibers = atomic_load_explicit(&sched->num_run_fibers, memory_order_relaxed);

  if (FEV_LIKELY(num_run_fibers >= sched->num_workers - num_waiting)) {
    if (!poller_waiting)
      atomic_store(&sched->poller_waiting, false);
    atomic_fetch_sub_explicit(&sched->num_waiting, 1, memory_order_relaxed);
    goto get_global;
  }

#ifdef FEV_POLLER_IO_URING
  fev_poller_wait(cur_worker);
  atomic_fetch_sub_explicit(&sched->num_waiting, 1, memory_order_relaxed);
  backoff = fev_bounded_spmc_queue_size(run_queue);
  goto get_local;
#else
  if (!poller_waiting) {
    fev_poller_wait(cur_worker);

    atomic_store_explicit(&sched->poller_waiting, false, memory_order_relaxed);
    atomic_fetch_sub_explicit(&sched->num_waiting, 1, memory_order_relaxed);

    backoff = fev_bounded_spmc_queue_size(&cur_worker->run_queue);
    goto get_local;
  } else {
    fev_poller_quiescent(cur_worker);
    fev_thr_sem_wait(&sched->sem);
    atomic_fetch_sub_explicit(&sched->num_waiting, 1, memory_order_relaxed);
    goto get_global;
  }
#endif

out:
  fev_sched_wake_all_workers(sched);
}

FEV_COLD FEV_NONNULL(1, 2) int fev_sched_put(struct fev_sched *sched, struct fev_fiber *fiber)
{
  /* 'sched' should be initialized and thus 'num_workers' should be positive. */
  FEV_ASSERT(sched->num_workers > 0);

  /* Put the fiber into the first worker's run queue (or fallback if there is not enough space). */
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
  uint32_t n;
  int ret;

  FEV_ASSERT(num_workers > 0);

  workers =
      fev_aligned_alloc(alignof(struct fev_sched_worker), (size_t)num_workers * sizeof(*workers));
  if (FEV_UNLIKELY(workers == NULL))
    return -ENOMEM;

  for (n = 0; n < num_workers; n++) {
    struct fev_sched_worker *worker = &workers[n];

    ret = fev_bounded_spmc_queue_init(&worker->run_queue,
                                      FEV_SCHED_STEAL_BOUNDED_SPMC_QUEUE_CAPACITY);
    if (FEV_UNLIKELY(ret != 0))
      goto fail_workers;

    worker->sched = sched;
    worker->rnd = (uint32_t)rand();
  }

  sched->workers = workers;
  sched->num_workers = num_workers;
  return 0;

fail_workers:
  for (uint32_t i = 0; i < n; i++)
    fev_bounded_spmc_queue_fini(&workers[i].run_queue);

  fev_aligned_free(workers);
  return ret;
}

FEV_COLD FEV_NONNULL(1) static void fev_sched_fini_workers(struct fev_sched *sched)
{
  for (uint32_t i = 0; i < sched->num_workers; i++)
    fev_bounded_spmc_queue_fini(&sched->workers[i].run_queue);
  fev_aligned_free(sched->workers);
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

  ret = fev_thr_mutex_init(&sched->fallback_queue_lock);
  if (FEV_UNLIKELY(ret != 0))
    goto fail_sem;

  STAILQ_INIT(&sched->fallback_queue);
  atomic_init(&sched->fallback_queue_len, 0);

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
  fev_sched_fini_workers(sched);

fail:
  return ret;
}

FEV_COLD FEV_NONNULL(1) void fev_sched_fini(struct fev_sched *sched)
{
  fev_thr_mutex_fini(&sched->fallback_queue_lock);
  fev_thr_sem_fini(&sched->sem);
  fev_timers_fini(&sched->timers);
  fev_poller_fini(sched);
  fev_sched_fini_workers(sched);
}
