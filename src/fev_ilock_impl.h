/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_ILOCK_IMPL_H
#define FEV_ILOCK_IMPL_H

#include "fev_ilock_intf.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

#include <queue.h>

#include "fev_compiler.h"
#include "fev_fiber.h"
#include "fev_sched_impl.h"
#include "fev_spinlock_impl.h"
#include "fev_thr_mutex.h"

/* TODO: Maybe rename the functions... */
#if defined(FEV_ILOCK_LOCK_MUTEX)
#define fev_ilock_lock_init fev_thr_mutex_init
#define fev_ilock_lock_fini fev_thr_mutex_fini
#define fev_ilock_lock_lock fev_thr_mutex_lock
#define fev_ilock_lock_unlock fev_thr_mutex_unlock
#elif defined(FEV_ILOCK_LOCK_SPINLOCK)
#define fev_ilock_lock_init fev_spinlock_init
#define fev_ilock_lock_fini fev_spinlock_fini
#define fev_ilock_lock_lock fev_spinlock_lock
#define fev_ilock_lock_unlock fev_spinlock_unlock
#endif

FEV_NONNULL(1) FEV_WARN_UNUSED_RESULT static inline int fev_ilock_init(struct fev_ilock *ilock)
{
  int ret = fev_ilock_lock_init(&ilock->lock);
  if (FEV_UNLIKELY(ret != 0))
    return ret;

  atomic_init(&ilock->state, 0);
  STAILQ_INIT(&ilock->waiters);
  return 0;
}

FEV_NONNULL(1) static inline void fev_ilock_fini(struct fev_ilock *ilock)
{
  fev_ilock_lock_fini(&ilock->lock);
}

/*
 * Locks the internal lock. Returns true, if we switched back to the scheduler and were blocked for
 * some time. Returns false, if the internal lock was acquired without switching and blocking. After
 * returning, the internal lock is owned by the caller, there are no spurious wake ups.
 */
FEV_NONNULL(1) static inline bool fev_ilock_lock(struct fev_ilock *ilock)
{
  unsigned expected = 0;
  bool success;

  success = atomic_compare_exchange_weak_explicit(&ilock->state, &expected, 1, memory_order_acquire,
                                                  memory_order_relaxed);
  if (FEV_LIKELY(success))
    return false;
  return fev_ilock_lock_slow(ilock);
}

/*
 * Unlocks the internal lock and returns the next fiber in the waiters queue, i.e. the fiber that
 * now owns the lock and should be woken up by the caller, or returns NULL if there is no such
 * fiber.
 */
FEV_NONNULL(1)
FEV_WARN_UNUSED_RESULT static inline struct fev_fiber *fev_ilock_unlock(struct fev_ilock *ilock)
{
  unsigned expected = 1;
  bool success;

  success = atomic_compare_exchange_strong_explicit(&ilock->state, &expected, 0,
                                                    memory_order_release, memory_order_relaxed);
  if (FEV_LIKELY(success))
    return NULL;
  return fev_ilock_unlock_slow(ilock);
}

/* Unlocks the internal lock and wakes up the next fiber in the waiters queue. */
FEV_NONNULL(1) static inline void fev_ilock_unlock_and_wake(struct fev_ilock *ilock)
{
  struct fev_fiber *fiber;

  fiber = fev_ilock_unlock(ilock);
  if (fiber != NULL)
    fev_cur_wake_one(fiber);
}

#endif /* !FEV_ILOCK_IMPL_H */
