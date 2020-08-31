/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#include "fev_sched_intf.h"

#include <errno.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "fev_alloc.h"
#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_os.h"
#include "fev_poller.h"
#include "fev_sched_attr.h"
#include "fev_thr.h"
#include "fev_thr_sem.h"

FEV_NONNULL(1)
void fev_wake_workers_slow(struct fev_sched_worker *worker, uint32_t num_waiting,
                           uint32_t num_fibers)
{
  struct fev_sched *sched = worker->sched;
  uint32_t n = num_fibers;

  if (n > num_waiting)
    n = num_waiting;

#ifdef FEV_POLLER_IO_URING
  while (n--)
    fev_poller_interrupt(&sched->poller);
#else
  if (atomic_load(&sched->poller_waiting)) {
    fev_poller_interrupt(&sched->poller);
    n--;
  }

  while (n--)
    fev_thr_sem_post(&sched->sem);
#endif
}

FEV_NONNULL(1) void fev_sched_wake_all_workers(struct fev_sched *sched)
{
#ifdef FEV_POLLER_IO_URING
  for (uint32_t i = 0; i < sched->num_workers; i++)
    fev_poller_interrupt(&sched->poller);
#else
  fev_poller_interrupt(&sched->poller);
  for (uint32_t i = 0; i < sched->num_workers; i++)
    fev_thr_sem_post(&sched->sem);
#endif
}

FEV_COLD FEV_NONNULL(1) static void fev_sched_work_start(struct fev_sched_worker *cur_worker)
{
  fev_cur_sched_worker = cur_worker;
  fev_sched_work(cur_worker);
}

FEV_COLD FEV_NONNULL(1) static void *fev_sched_thread_proc(void *arg)
{
  struct fev_sched_worker *cur_worker = arg;

  /* Do not try to do any work before all threads are successfully created. */
  fev_thr_sem_wait(cur_worker->sched->start_sem);

  fev_sched_work_start(cur_worker);

  return NULL;
}

FEV_COLD FEV_NONNULL(1) int fev_sched_run(struct fev_sched *sched)
{
  struct fev_thr_sem start_sem;
  struct fev_thr *thrs;
  uint32_t num_workers = sched->num_workers;
  uint32_t n; /* number of successfully created workers */
  int ret;

  FEV_ASSERT(num_workers > 0);

  ret = fev_thr_sem_init(&start_sem, 0);
  if (FEV_UNLIKELY(ret != 0))
    goto out;

  sched->start_sem = &start_sem;

  ret = -ENOMEM;
  thrs = fev_malloc(num_workers * sizeof(*thrs));
  if (FEV_UNLIKELY(thrs == NULL))
    goto out_sem;

  for (n = 1; n < num_workers; n++) {
    ret = fev_thr_create(&thrs[n], &fev_sched_thread_proc, &sched->workers[n]);
    if (FEV_UNLIKELY(ret != 0))
      goto fail_thrs;
  }

  /* Allow other workers to start executing. */
  for (uint32_t i = 1; i < num_workers; i++)
    fev_thr_sem_post(&start_sem);

  fev_sched_work_start(&sched->workers[0]);

  ret = 0;
  goto out_thrs;

fail_thrs:
  for (uint32_t i = 1; i < n; i++)
    fev_thr_cancel(&thrs[i]);

out_thrs:
  for (uint32_t i = 1; i < n; i++)
    fev_thr_join(&thrs[i], NULL);

  fev_free(thrs);

out_sem:
  fev_thr_sem_fini(&start_sem);
  sched->start_sem = NULL;

out:
  return ret;
}

FEV_COLD FEV_NONNULL(1) int fev_sched_create(struct fev_sched **sched_ptr,
                                             const struct fev_sched_attr *attr)
{
  struct fev_sched *sched;
  uint32_t num_workers;
  int ret;

  if (attr == NULL)
    attr = &fev_sched_default_attr;

  sched = fev_aligned_alloc(FEV_DCACHE_LINE_SIZE, sizeof(*sched));
  if (FEV_UNLIKELY(sched == NULL))
    return -ENOMEM;

  num_workers = attr->num_workers;
  if (num_workers == 0)
    num_workers = fev_get_num_processors();

  ret = fev_sched_init(sched, num_workers);
  if (FEV_UNLIKELY(ret != 0)) {
    fev_aligned_free(sched);
    return ret;
  }

  *sched_ptr = sched;
  return 0;
}

FEV_COLD FEV_NONNULL(1) void fev_sched_destroy(struct fev_sched *sched)
{
  fev_sched_fini(sched);
  fev_aligned_free(sched);
}
