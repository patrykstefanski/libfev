/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#include "fev_alloc.h"

#include <stdlib.h>

#include "fev_compiler.h"

fev_realloc_t fev_realloc_ptr = &realloc;

fev_realloc_t fev_get_realloc(void) { return fev_realloc_ptr; }

FEV_NONNULL(1) void fev_set_realloc(fev_realloc_t realloc_ptr) { fev_realloc_ptr = realloc_ptr; }
