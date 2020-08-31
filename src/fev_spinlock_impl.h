/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_SPINLOCK_IMPL_H
#define FEV_SPINLOCK_IMPL_H

#include "fev_spinlock_intf.h"

#include <stdatomic.h>
#include <stdbool.h>

#include "fev_arch.h"
#include "fev_compiler.h"

FEV_NONNULL(1) static inline int fev_spinlock_init(struct fev_spinlock *spinlock)
{
  atomic_init(&spinlock->state, 0);
  return 0;
}

FEV_NONNULL(1) static inline void fev_spinlock_fini(struct fev_spinlock *spinlock)
{
  (void)spinlock;
}

FEV_NONNULL(1) static inline bool fev_spinlock_try_lock(struct fev_spinlock *spinlock)
{
  if (atomic_load_explicit(&spinlock->state, memory_order_relaxed) != 0)
    return false;
  if (atomic_exchange_explicit(&spinlock->state, 1, memory_order_acquire) != 0)
    return false;
  return true;
}

FEV_NONNULL(1) static inline void fev_spinlock_lock(struct fev_spinlock *spinlock)
{
  while (atomic_exchange_explicit(&spinlock->state, 1, memory_order_acquire) != 0) {
    while (atomic_load_explicit(&spinlock->state, memory_order_relaxed) != 0)
      fev_pause();
  }
}

FEV_NONNULL(1) static inline void fev_spinlock_unlock(struct fev_spinlock *spinlock)
{
  atomic_store_explicit(&spinlock->state, 0, memory_order_release);
}

#endif /* !FEV_SPINLOCK_IMPL_H */
