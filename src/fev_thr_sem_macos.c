/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#include "fev_thr_sem.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "fev_assert.h"
#include "fev_compiler.h"

FEV_NONNULL(1) FEV_WARN_UNUSED_RESULT int fev_thr_sem_init(struct fev_thr_sem *sem, unsigned value)
{
  char name[64];
  pid_t pid;
  sem_t *handle;

  FEV_ASSERT(value <= SEM_VALUE_MAX);

  /* TODO: This can leak some information, like ASLR offset etc. */
  pid = getpid();
  snprintf(name, sizeof(name), "fev_sem_%i_%016" PRIxPTR, pid, (uintptr_t)sem);
  handle = sem_open(name, O_CREAT | O_EXCL, 0644, value);
  if (FEV_UNLIKELY(handle == SEM_FAILED))
    return -errno;

  sem->handle = handle;
  return 0;
}

FEV_NONNULL(1) void fev_thr_sem_fini(struct fev_thr_sem *sem)
{
  int ret = sem_close(sem->handle);
  (void)ret;
  FEV_ASSERT(ret == 0);
}
