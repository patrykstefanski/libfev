/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_THR_POSIX_H
#define FEV_THR_POSIX_H

#include <errno.h>
#include <pthread.h>
#include <stddef.h>

#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_util.h"

struct fev_thr {
  pthread_t handle;
};

FEV_NONNULL(1, 2)
FEV_WARN_UNUSED_RESULT
static inline int fev_thr_create(struct fev_thr *thr, void *(*start_routine)(void *), void *arg)
{
  int ret = pthread_create(&thr->handle, NULL, start_routine, arg);
  FEV_ASSERT(ret != EINVAL);
  return -ret;
}

FEV_NONNULL(1) static inline void fev_thr_join(struct fev_thr *thr, void **ret_val_ptr)
{
  int ret = pthread_join(thr->handle, ret_val_ptr);
  (void)ret;
  FEV_ASSERT(ret == 0);
}

FEV_NONNULL(1) static inline int fev_thr_cancel(struct fev_thr *thr)
{
  int ret = pthread_cancel(thr->handle);
  return -ret;
}

#endif /* !FEV_THR_POSIX_H */
