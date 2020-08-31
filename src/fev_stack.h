/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_STACK_H
#define FEV_STACK_H

#include <stddef.h>

int fev_stack_alloc(void **addr_ptr, size_t usable_size, size_t guard_size);
void fev_stack_free(void *addr, size_t total_size);

#endif /* !FEV_STACK_H */
