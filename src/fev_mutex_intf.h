/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_MUTEX_INTF_H
#define FEV_MUTEX_INTF_H

#include <fev/fev.h>

#include <stdatomic.h>

#include "fev_waiters_queue_intf.h"

struct fev_mutex {
  /*
   * State of the lock:
   * 0 - unlocked
   * 1 - locked, no waiters
   * 2 - locked, some waiters
   */
  atomic_uint state;

  struct fev_waiters_queue wq;
};

#endif /* !FEV_MUTEX_INTF_H */
