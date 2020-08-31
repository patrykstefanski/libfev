/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_WAITERS_QUEUE_INTF_H
#define FEV_WAITERS_QUEUE_INTF_H

#include <stdbool.h>

#include <queue.h>

#include "fev_ilock_intf.h"
#include "fev_waiter_intf.h"

struct fev_waiters_queue_node {
  struct fev_waiter waiter;
  TAILQ_ENTRY(fev_waiters_queue_node) tq_entry;
  bool deleted;
};

struct fev_waiters_queue {
  struct fev_ilock lock;
  TAILQ_HEAD(, fev_waiters_queue_node) nodes;
};

#endif /* !FEV_WAITERS_QUEUE_INTF_H */
