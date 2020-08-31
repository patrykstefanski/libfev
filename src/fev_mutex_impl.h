/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_MUTEX_IMPL_H
#define FEV_MUTEX_IMPL_H

#include "fev_mutex_intf.h"

#include <stdatomic.h>

#include "fev_compiler.h"
#include "fev_waiters_queue_impl.h"

FEV_NONNULL(1) FEV_WARN_UNUSED_RESULT static inline int fev_mutex_init(struct fev_mutex *mutex)
{
  int ret = fev_waiters_queue_init(&mutex->wq);
  if (FEV_UNLIKELY(ret != 0))
    return ret;

  atomic_init(&mutex->state, 0);
  return 0;
}

FEV_NONNULL(1) static inline void fev_mutex_fini(struct fev_mutex *mutex)
{
  fev_waiters_queue_fini(&mutex->wq);
}

#endif /* !FEV_MUTEX_IMPL_H */
