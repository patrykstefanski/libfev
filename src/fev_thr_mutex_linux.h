/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_THR_MUTEX_LINUX_H
#define FEV_THR_MUTEX_LINUX_H

#include <stdatomic.h>
#include <stdbool.h>

#include "fev_compiler.h"

/*
 * glibc uses a similar implementation, however the atomic instructions cannot be inlined unless
 * linked statically. Here, these instructions will be inlined and this improves performance a bit.
 *
 * Based on 'Futexes Are Tricky' by Ulrich Drepper.
 */

struct fev_thr_mutex {
  atomic_int state;
};

FEV_NONNULL(1) void fev_thr_mutex_lock_slow(struct fev_thr_mutex *mutex);

FEV_NONNULL(1) void fev_thr_mutex_unlock_slow(struct fev_thr_mutex *mutex);

FEV_NONNULL(1) static inline int fev_thr_mutex_init(struct fev_thr_mutex *mutex)
{
  atomic_init(&mutex->state, 0);
  return 0;
}

FEV_NONNULL(1) static inline void fev_thr_mutex_fini(struct fev_thr_mutex *mutex) { (void)mutex; }

FEV_NONNULL(1) static inline bool fev_thr_mutex_try_lock(struct fev_thr_mutex *mutex)
{
  int expected = 0;
  return atomic_compare_exchange_weak_explicit(&mutex->state, &expected, 1, memory_order_acquire,
                                               memory_order_relaxed);
}

FEV_NONNULL(1) static inline void fev_thr_mutex_lock(struct fev_thr_mutex *mutex)
{
  int expected = 0;
  bool success = atomic_compare_exchange_weak_explicit(&mutex->state, &expected, 1,
                                                       memory_order_acquire, memory_order_relaxed);
  if (FEV_UNLIKELY(!success))
    fev_thr_mutex_lock_slow(mutex);
}

FEV_NONNULL(1) static inline void fev_thr_mutex_unlock(struct fev_thr_mutex *mutex)
{
  int state = atomic_exchange_explicit(&mutex->state, 0, memory_order_release);
  if (FEV_UNLIKELY(state == 2))
    fev_thr_mutex_unlock_slow(mutex);
}

#endif /* !FEV_THR_MUTEX_LINUX_H */
