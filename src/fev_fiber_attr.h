/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_FIBER_ATTR_H
#define FEV_FIBER_ATTR_H

#include <fev/fev.h>

#include <stdbool.h>
#include <stddef.h>

struct fev_fiber_attr {
  void *stack_addr;
  size_t stack_size;
  size_t guard_size;
  bool detached;
};

extern const struct fev_fiber_attr fev_fiber_create_default_attr;
extern const struct fev_fiber_attr fev_fiber_spawn_default_attr;

#endif /* !FEV_FIBER_ATTR_H */
