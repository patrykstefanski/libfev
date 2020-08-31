/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_ASSERT_H
#define FEV_ASSERT_H

#include <fev/fev.h>

#include <stdio.h>
#include <stdlib.h>

#include "fev_compiler.h"

#ifdef FEV_ENABLE_DEBUG_ASSERT

#define FEV_ASSERT(expr)                                                                           \
  do {                                                                                             \
    if (FEV_UNLIKELY(!(expr))) {                                                                   \
      fprintf(stderr, "Asseration \"%s\" failed in %s (%s:%u)\n", #expr, __func__, __FILE__,       \
              __LINE__);                                                                           \
      abort();                                                                                     \
    }                                                                                              \
  } while (0)

#else

#define FEV_ASSERT(expr) ((void)0)

#endif

#endif /* !FEV_ASSERT_H */
