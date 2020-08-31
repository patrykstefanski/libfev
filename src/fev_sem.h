/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_SEM_H
#define FEV_SEM_H

#include <fev/fev.h>

#include <stdint.h>

#include "fev_waiters_queue_intf.h"

struct fev_sem {
  int32_t value;
  struct fev_waiters_queue wq;
};

#endif /* !FEV_SEM_H */
