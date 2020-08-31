/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_SPINLOCK_INTF_H
#define FEV_SPINLOCK_INTF_H

#include <stdatomic.h>

struct fev_spinlock {
  atomic_uint state;
};

#endif /* !FEV_SPINLOCK_INTF_H */
