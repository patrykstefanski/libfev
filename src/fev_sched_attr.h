/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_SCHED_ATTR_H
#define FEV_SCHED_ATTR_H

#include <fev/fev.h>

#include <stdint.h>

struct fev_sched_attr {
  uint32_t num_workers;
};

extern const struct fev_sched_attr fev_sched_default_attr;

#endif /* !FEV_SCHED_ATTR_H */
