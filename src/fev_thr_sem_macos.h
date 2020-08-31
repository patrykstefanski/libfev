/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_THR_SEM_MACOS_H
#define FEV_THR_SEM_MACOS_H

#include <errno.h>
#include <semaphore.h>

#include "fev_assert.h"
#include "fev_compiler.h"

struct fev_thr_sem {
  sem_t *handle;
};

FEV_NONNULL(1) FEV_WARN_UNUSED_RESULT int fev_thr_sem_init(struct fev_thr_sem *sem, unsigned value);

FEV_NONNULL(1) void fev_thr_sem_fini(struct fev_thr_sem *sem);

FEV_NONNULL(1) static inline void fev_thr_sem_wait(struct fev_thr_sem *sem)
{
  int ret;
  do {
    ret = sem_wait(sem->handle);
    FEV_ASSERT(ret == 0 || (ret == -1 && errno == EINTR));
  } while (ret != 0);
}

FEV_NONNULL(1) static inline void fev_thr_sem_post(struct fev_thr_sem *sem)
{
  int ret = sem_post(sem->handle);
  (void)ret;
  FEV_ASSERT(ret == 0);
}

#endif /* !FEV_THR_SEM_MACOS_H */
