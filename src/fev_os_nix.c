/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#include "fev_os.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fev_compiler.h"

uint32_t fev_get_num_processors(void)
{
  long value;

  errno = 0;
  value = sysconf(_SC_NPROCESSORS_ONLN);
  if (FEV_UNLIKELY(value == -1)) {
    /* errno theoretically can be 0 here. */
    fprintf(stderr, "Failed to get number of processors: %s\n", strerror(errno));
    abort();
  }

  if (FEV_UNLIKELY(value < 1)) {
    fprintf(stderr, "Got %li as number of processors, should be at least 1\n", value);
    abort();
  }

#if LONG_MAX > UINT32_MAX
  if (FEV_UNLIKELY(value >= UINT32_MAX))
    value = UINT32_MAX;
#endif

  return (uint32_t)value;
}
