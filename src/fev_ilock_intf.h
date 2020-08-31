/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_ILOCK_INTF_H
#define FEV_ILOCK_INTF_H

#include <stdatomic.h>
#include <stdbool.h>

#include <queue.h>

#include "fev_compiler.h"
#include "fev_spinlock_intf.h"
#include "fev_thr_mutex.h"

#if defined(FEV_ILOCK_LOCK_MUTEX)
typedef struct fev_thr_mutex fev_ilock_lock_t;
#elif defined(FEV_ILOCK_LOCK_SPINLOCK)
typedef struct fev_spinlock fev_ilock_lock_t;
#else
#error Wrong lock strategy for ilock selected, define either FEV_ILOCK_LOCK_MUTEX or \
        FEV_ILOCK_LOCK_SPINLOCK.
#endif

/* Internal lock. It is used to implement higher-level primitives. */

struct fev_fiber;

struct fev_ilock {
  /*
   * State of the lock:
   * 0 - unlocked
   * 1 - locked, no waiters
   * 2 - locked, some waiters
   */
  atomic_uint state;

  fev_ilock_lock_t lock;
  STAILQ_HEAD(, fev_fiber) waiters;
};

/* Slow path variants, don't use directly. Use the functions defined in fev_ilock_impl.h. */

FEV_NONNULL(1) bool fev_ilock_lock_slow(struct fev_ilock *ilock);

FEV_NONNULL(1)
FEV_RETURNS_NONNULL FEV_WARN_UNUSED_RESULT struct fev_fiber *
fev_ilock_unlock_slow(struct fev_ilock *ilock);

#endif /* !FEV_ILOCK_INTF_H */
