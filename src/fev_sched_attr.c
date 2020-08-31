/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#include "fev_sched_attr.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fev_alloc.h"
#include "fev_compiler.h"

const struct fev_sched_attr fev_sched_default_attr = {
    .num_workers = 0u,
};

FEV_NONNULL(1) int fev_sched_attr_create(struct fev_sched_attr **attr_ptr)
{
  struct fev_sched_attr *attr;

  attr = fev_malloc(sizeof(*attr));
  if (FEV_UNLIKELY(attr == NULL))
    return -ENOMEM;

  memset(attr, 0, sizeof(*attr));
  attr->num_workers = fev_sched_default_attr.num_workers;

  *attr_ptr = attr;
  return 0;
}

FEV_NONNULL(1) void fev_sched_attr_destroy(struct fev_sched_attr *attr) { fev_free(attr); }

FEV_NONNULL(1) FEV_PURE uint32_t fev_sched_attr_get_num_workers(const struct fev_sched_attr *attr)
{
  return attr->num_workers;
}

FEV_NONNULL(1)
void fev_sched_attr_set_num_workers(struct fev_sched_attr *attr, uint32_t num_workers)
{
  attr->num_workers = num_workers;
}
