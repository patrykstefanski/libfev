/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_THR_SEM_WIN_H
#define FEV_THR_SEM_WIN_H

#include <limits.h>
#include <stddef.h>
#include <windows.h>

#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_util.h"

struct fev_thr_sem {
  HANDLE handle;
};

FEV_NONNULL(1)
FEV_WARN_UNUSED_RESULT static int fev_thr_sem_init(struct fev_thr_sem *sem, unsigned value)
{
  sem->handle = CreateSemaphore(/*lpSemaphoreAttributes=*/NULL, /*lInitialCount=*/(LONG)value,
                                /*lMaximumCount=*/(LONG)UINT_MAX, /*lpName=*/NULL);
  if (FEV_UNLIKELY(handle == NULL))
    return fev_translate_win_error(GetLastError());
  return 0;
}

FEV_NONNULL(1) static void fev_thr_sem_fini(struct fev_thr_sem *sem)
{
  BOOL ok = CloseHandle(sem->handle);
  (void)ok;
  FEV_ASSERT(ok);
}

FEV_NONNULL(1) static void fev_thr_sem_wait(struct fev_thr_sem *sem)
{
  DWORD result = WaitForSingleObject(sem->handle, /*dwMilliseconds=*/INFINITE);
  (void)result;
  FEV_ASSERT(result == WAIT_OBJECT_0);
}

FEV_NONNULL(1) static void fev_thr_sem_post(struct fev_thr_sem *sem)
{
  BOOL ok = ReleaseSemaphore(sem->handle, /*lReleaseCount=*/1, /*lpPreviousCount=*/NULL);
  (void)ok;
  FEV_ASSERT(ok);
}

#endif /* !FEV_THR_SEM_WIN_H */
