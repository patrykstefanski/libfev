/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_COND_IMPL_H
#define FEV_COND_IMPL_H

#include "fev_cond_intf.h"

#include "fev_compiler.h"
#include "fev_waiters_queue_impl.h"

FEV_NONNULL(1) FEV_WARN_UNUSED_RESULT static inline int fev_cond_init(struct fev_cond *cond)
{
  return fev_waiters_queue_init(&cond->wq);
}

FEV_NONNULL(1) static inline void fev_cond_fini(struct fev_cond *cond)
{
  fev_waiters_queue_fini(&cond->wq);
}

#endif /* !FEV_COND_IMPL_H */
