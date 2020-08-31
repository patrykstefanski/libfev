/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_THR_MUTEX_WIN_H
#define FEV_THR_MUTEX_WIN_H

#include <stdbool.h>
#include <windows.h>

#include "fev_compiler.h"

struct fev_thr_mutex {
  CRITICAL_SECTION critical_section;
};

FEV_NONNULL(1) static inline int fev_thr_mutex_init(struct fev_thr_mutex *mutex)
{
  InitializeCriticalSection(&mutex->critical_section);
  return 0;
}

FEV_NONNULL(1) static inline void fev_thr_mutex_fini(struct fev_thr_mutex *mutex)
{
  DeleteCriticalSection(&mutex->critical_section);
}

FEV_NONNULL(1) static inline bool fev_thr_mutex_try_lock(struct fev_thr_mutex *mutex)
{
  return TryEnterCriticalSection(&mutex->critical_section);
}

FEV_NONNULL(1) static inline void fev_thr_mutex_lock(struct fev_thr_mutex *mutex)
{
  EnterCriticalSection(&mutex->critical_section);
}

FEV_NONNULL(1) static inline void fev_thr_mutex_unlock(struct fev_thr_mutex *mutex)
{
  LeaveCriticalSection(&mutex->critical_section);
}

#endif /* !FEV_THR_MUTEX_WIN_H */
