/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#include "fev_cond_impl.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fev_alloc.h"
#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_mutex_intf.h"
#include "fev_time.h"
#include "fev_waiters_queue_impl.h"

FEV_NONNULL(1) int fev_cond_create(struct fev_cond **cond_ptr)
{
  struct fev_cond *cond;
  int ret;

  cond = fev_malloc(sizeof(*cond));
  if (FEV_UNLIKELY(cond == NULL))
    return -ENOMEM;

  ret = fev_cond_init(cond);
  if (FEV_UNLIKELY(ret != 0)) {
    fev_free(cond);
    return ret;
  }

  *cond_ptr = cond;
  return 0;
}

FEV_NONNULL(1) void fev_cond_destroy(struct fev_cond *cond)
{
  fev_cond_fini(cond);
  fev_free(cond);
}

static bool fev_cond_wait_recheck(void *arg)
{
  struct fev_mutex *mutex = arg;

  fev_mutex_unlock(mutex);
  return true;
}

FEV_NONNULL(1, 2) void fev_cond_wait(struct fev_cond *cond, struct fev_mutex *mutex)
{
  int res = fev_waiters_queue_wait(&cond->wq, /*abs_time=*/NULL, &fev_cond_wait_recheck, mutex);
  (void)res;
  FEV_ASSERT(res == 0);

  fev_mutex_lock(mutex);
}

FEV_NONNULL(1, 2, 3)
int fev_cond_wait_until(struct fev_cond *cond, struct fev_mutex *mutex,
                        const struct timespec *abs_time)
{
  int res = fev_waiters_queue_wait(&cond->wq, abs_time, &fev_cond_wait_recheck, mutex);

  if (res == -ENOMEM || res == -ETIMEDOUT)
    return res;

  /*
   * We were woken up by fev_cond_notify_one()/all() or spuriously, in both cases we need to
   * reacquire the mutex.
   */
  FEV_ASSERT(res == 0 || res == -EAGAIN);
  return fev_mutex_try_lock_until(mutex, abs_time);
}

FEV_NONNULL(1, 2, 3)
int fev_cond_wait_for(struct fev_cond *cond, struct fev_mutex *mutex,
                      const struct timespec *rel_time)
{
  struct timespec abs_time;

  fev_get_abs_time_since_now(&abs_time, rel_time);
  return fev_cond_wait_until(cond, mutex, &abs_time);
}

FEV_NONNULL(1) void fev_cond_notify_one(struct fev_cond *cond)
{
  fev_waiters_queue_wake(&cond->wq, /*max_waiters=*/1, /*callback=*/NULL, /*callback_arg=*/NULL);
}

FEV_NONNULL(1) void fev_cond_notify_all(struct fev_cond *cond)
{
  fev_waiters_queue_wake(&cond->wq, /*max_waiters=*/UINT32_MAX, /*callback=*/NULL,
                         /*callback_arg=*/NULL);
}
