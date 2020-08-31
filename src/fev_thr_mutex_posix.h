/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_THR_MUTEX_POSIX_H
#define FEV_THR_MUTEX_POSIX_H

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_util.h"

struct fev_thr_mutex {
  pthread_mutex_t handle;
};

FEV_NONNULL(1)
FEV_WARN_UNUSED_RESULT static inline int fev_thr_mutex_init(struct fev_thr_mutex *mutex)
{
  int ret = pthread_mutex_init(&mutex->handle, NULL);
  FEV_ASSERT(ret != EBUSY);
  FEV_ASSERT(ret != EINVAL);
  return FEV_LIKELY(ret == 0) ? 0 : -ret;
}

/*
 * MacOS, Darwin, Linux, DragonflyBSD, NetBSD and OpenBSD won't fail unless we make a mistake, thus
 * an assertion should suffice. FreeBSD can fail with ENOTRECOVERABLE.
 */
#if !defined(FEV_OS_MACOS) && !defined(FEV_OS_DARWIN) && !defined(FEV_OS_LINUX) &&                 \
    !defined(FEV_OS_DRAGONFLYBSD) && !defined(FEV_OS_NETBSD) && !defined(FEV_OS_OPENBSD)
#define FEV_THR_MUTEX_CAN_FAIL
#endif

#ifdef FEV_THR_MUTEX_CAN_FAIL
#define FEV_THR_MUTEX_CHECK(e)                                                                     \
  do {                                                                                             \
    if (!(e)) {                                                                                    \
      fprintf(stderr, #e " failed in %s\n", __func__);                                             \
      abort();                                                                                     \
    }                                                                                              \
  } while (0)
#else
#define FEV_THR_MUTEX_CHECK(e) FEV_ASSERT(e)
#endif

FEV_NONNULL(1) static inline void fev_thr_mutex_fini(struct fev_thr_mutex *mutex)
{
  int ret = pthread_mutex_destroy(&mutex->handle);
  (void)ret;
  FEV_THR_MUTEX_CHECK(ret == 0);
}

FEV_NONNULL(1) static inline bool fev_thr_mutex_try_lock(struct fev_thr_mutex *mutex)
{
  int ret = pthread_mutex_trylock(&mutex->handle);
  FEV_THR_MUTEX_CHECK(ret == 0 || ret == EBUSY);
  return !ret;
}

FEV_NONNULL(1) static inline void fev_thr_mutex_lock(struct fev_thr_mutex *mutex)
{
  int ret = pthread_mutex_lock(&mutex->handle);
  (void)ret;
  FEV_THR_MUTEX_CHECK(ret == 0);
}

FEV_NONNULL(1) static inline void fev_thr_mutex_unlock(struct fev_thr_mutex *mutex)
{
  int ret = pthread_mutex_unlock(&mutex->handle);
  (void)ret;
  FEV_THR_MUTEX_CHECK(ret == 0);
}

#endif /* !FEV_THR_MUTEX_POSIX_H */
