/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#include "fev_sem.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fev_alloc.h"
#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_time.h"
#include "fev_waiters_queue_impl.h"

FEV_NONNULL(1) FEV_WARN_UNUSED_RESULT static int fev_sem_init(struct fev_sem *sem, int32_t value)
{
  int ret = fev_waiters_queue_init(&sem->wq);
  if (FEV_UNLIKELY(ret != 0))
    return ret;

  sem->value = value;
  return 0;
}

FEV_NONNULL(1) static void fev_sem_fini(struct fev_sem *sem) { fev_waiters_queue_fini(&sem->wq); }

FEV_NONNULL(1) int fev_sem_create(struct fev_sem **sem_ptr, int32_t value)
{
  struct fev_sem *sem;
  int ret;

  sem = fev_malloc(sizeof(*sem));
  if (FEV_UNLIKELY(sem == NULL))
    return -ENOMEM;

  ret = fev_sem_init(sem, value);
  if (FEV_UNLIKELY(ret != 0)) {
    fev_free(sem);
    return ret;
  }

  *sem_ptr = sem;
  return 0;
}

FEV_NONNULL(1) void fev_sem_destroy(struct fev_sem *sem)
{
  fev_sem_fini(sem);
  fev_free(sem);
}

static bool fev_sem_wait_recheck(void *arg)
{
  struct fev_sem *sem = arg;

  if (sem->value > 0) {
    sem->value--;
    return false;
  }

  return true;
}

FEV_NONNULL(1) void fev_sem_wait(struct fev_sem *sem)
{
  int res = fev_waiters_queue_wait(&sem->wq, /*abs_time=*/NULL, &fev_sem_wait_recheck, sem);
  (void)res;
  FEV_ASSERT(res == 0);
}

FEV_NONNULL(1, 2) int fev_sem_wait_until(struct fev_sem *sem, const struct timespec *abs_time)
{
  int res;

  do {
    res = fev_waiters_queue_wait(&sem->wq, abs_time, &fev_sem_wait_recheck, sem);
  } while (res == -EAGAIN);

  return res;
}

FEV_NONNULL(1, 2) int fev_sem_wait_for(struct fev_sem *sem, const struct timespec *rel_time)
{
  struct timespec abs_time;

  fev_get_abs_time_since_now(&abs_time, rel_time);
  return fev_sem_wait_until(sem, &abs_time);
}

static void fev_sem_post_callback(void *arg, uint32_t num_woken, bool is_empty)
{
  struct fev_sem *sem = arg;

  (void)is_empty;

  FEV_ASSERT(num_woken <= 1);

  if (num_woken == 0)
    sem->value++;
}

FEV_NONNULL(1) void fev_sem_post(struct fev_sem *sem)
{
  fev_waiters_queue_wake(&sem->wq, /*max_waiters=*/1, &fev_sem_post_callback, sem);
}
