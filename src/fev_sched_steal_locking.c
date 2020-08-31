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
#include "fev_spinlock_impl.h"
#include "fev_thr_mutex.h"
#include "fev_thr_sem.h"
#include "fev_util.h"

_Thread_local struct fev_sched_worker *fev_cur_sched_worker;

FEV_NONNULL(1)
static uint32_t fev_sched_steal(struct fev_sched_worker *worker)
{
  struct fev_sched *sched = worker->sched;
  struct fev_sched_worker *workers = sched->workers;
  uint32_t num_workers = sched->num_workers, rnd, num_stolen = 0;

  rnd = worker->rnd = FEV_RANDOM_NEXT(worker->rnd);

  for (uint32_t i = 0; i < num_workers; i++) {
    struct fev_sched_worker *victim;
    struct fev_sched_run_queue *victim_rq;
    struct fev_fiber *first, *cur, *last;

    victim = &workers[(rnd + i) % num_workers];

    /* Don't steal from ourselves, we don't have any fibers. */
    if (victim == worker)
      continue;

    victim_rq = &victim->run_queue;

    /*
     * Steal some fibers from victim.
     * TODO: This can steal all fibers.
     */

    fev_sched_run_queue_lock(&victim_rq->lock);

    first = cur = STAILQ_FIRST(&victim_rq->head);
    while (cur != NULL && num_stolen < FEV_SCHED_STEAL_LOCKING_STEAL_COUNT) {
      last = cur;
      cur = STAILQ_NEXT(cur, stq_entry);
      num_stolen++;
    }

    if (cur != NULL) {
      /* cur points to the first not stolen fiber. */
      STAILQ_FIRST(&victim_rq->head) = cur;
    } else {
      /* cur is NULL, we have stolen all fibers. */
      STAILQ_INIT(&victim_rq->head);
    }

    FEV_ASSERT(atomic_load(&victim_rq->size) >= num_stolen);
    atomic_fetch_sub_explicit(&victim_rq->size, num_stolen, memory_order_relaxed);

    fev_sched_run_queue_unlock(&victim_rq->lock);

    if (num_stolen > 0) {
      struct fev_sched_run_queue *run_queue = &worker->run_queue;

      FEV_ASSERT(first != NULL);

      fev_sched_run_queue_lock(&run_queue->lock);

      FEV_ASSERT(STAILQ_EMPTY(&run_queue->head));
      STAILQ_NEXT(last, stq_entry) = NULL;
      STAILQ_FIRST(&run_queue->head) = first;
      run_queue->head.stqh_last = &STAILQ_NEXT(last, stq_entry);

      FEV_ASSERT(atomic_load(&run_queue->size) == 0);
      atomic_store_explicit(&run_queue->size, num_stolen, memory_order_relaxed);

      fev_sched_run_queue_unlock(&run_queue->lock);

      break;
    }
  }

  return num_stolen;
}

FEV_ALIGNED(FEV_ICACHE_LINE_SIZE)
FEV_HOT FEV_NOINLINE FEV_NONNULL(1) void fev_sched_work(struct fev_sched_worker *cur_worker)
{
  struct fev_sched *sched = cur_worker->sched;
  struct fev_sched_run_queue *run_queue = &cur_worker->run_queue;
  struct fev_fiber *cur_fiber;
  uint32_t backoff = 0;

  goto get_local;

switch_to_fiber:
  cur_worker->cur_fiber = cur_fiber;
  fev_context_switch(&cur_worker->context, &cur_fiber->context);

  if (backoff == 0)
    goto get_global;

  --backoff;

get_local:
  fev_sched_run_queue_lock(&run_queue->lock);
  cur_fiber = STAILQ_FIRST(&run_queue->head);
  if (FEV_LIKELY(cur_fiber != NULL)) {
    STAILQ_REMOVE_HEAD(&run_queue->head, stq_entry);

    FEV_ASSERT(atomic_load(&run_queue->size) > 0);
    atomic_fetch_sub_explicit(&run_queue->size, 1, memory_order_relaxed);

    fev_sched_run_queue_unlock(&run_queue->lock);
    goto switch_to_fiber;
  }
  fev_sched_run_queue_unlock(&run_queue->lock);

get_global:
  fev_poller_check(cur_worker);

  backoff = atomic_load_explicit(&run_queue->size, memory_order_relaxed);
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
  backoff = atomic_load_explicit(&run_queue->size, memory_order_relaxed);
  goto get_local;
#else
  if (!poller_waiting) {
    fev_poller_wait(cur_worker);

    atomic_store_explicit(&sched->poller_waiting, false, memory_order_relaxed);
    atomic_fetch_sub_explicit(&sched->num_waiting, 1, memory_order_relaxed);

    backoff = atomic_load_explicit(&run_queue->size, memory_order_relaxed);
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
  struct fev_sched_run_queue *run_queue;

  /* 'sched' should be initialized and thus 'num_workers' should be positive. */
  FEV_ASSERT(sched->num_workers > 0);

  /* Put the fiber into the first worker's run queue. */
  run_queue = &sched->workers[0].run_queue;
  STAILQ_INSERT_TAIL(&run_queue->head, fiber, stq_entry);
  atomic_fetch_add_explicit(&run_queue->size, 1, memory_order_relaxed);

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

    ret = fev_sched_run_queue_lock_init(&worker->run_queue.lock);
    if (FEV_UNLIKELY(ret != 0))
      goto fail;

    worker->sched = sched;
    worker->rnd = (uint32_t)rand();
    STAILQ_INIT(&worker->run_queue.head);
    atomic_init(&worker->run_queue.size, 0);
  }

  sched->workers = workers;
  sched->num_workers = num_workers;
  return 0;

fail:
  while (n-- > 0) {
    struct fev_sched_worker *worker = &workers[n];
    fev_sched_run_queue_lock_fini(&worker->run_queue.lock);
  }

  fev_aligned_free(sched->workers);
  return ret;
}

FEV_COLD FEV_NONNULL(1) static void fev_sched_fini_workers(struct fev_sched *sched)
{
  for (uint32_t i = 0; i < sched->num_workers; i++)
    fev_sched_run_queue_lock_fini(&sched->workers[i].run_queue.lock);
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

fail:
  return ret;
}

FEV_COLD FEV_NONNULL(1) void fev_sched_fini(struct fev_sched *sched)
{
  fev_thr_sem_fini(&sched->sem);
  fev_timers_fini(&sched->timers);
  fev_poller_fini(sched);
  fev_sched_fini_workers(sched);
}
