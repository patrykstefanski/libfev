/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_COND_INTF_H
#define FEV_COND_INTF_H

#include <fev/fev.h>

#include "fev_waiters_queue_intf.h"

struct fev_cond {
  struct fev_waiters_queue wq;
};

#endif /* !FEV_COND_INTF_H */
