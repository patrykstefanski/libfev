/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#include "fev_fiber_attr.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fev_alloc.h"
#include "fev_compiler.h"

static_assert(FEV_DEFAULT_STACK_SIZE % FEV_PAGE_SIZE == 0,
              "FEV_DEFAULT_STACK_SIZE must be multiple of FEV_PAGE_SIZE");
static_assert(FEV_DEFAULT_GUARD_SIZE % FEV_PAGE_SIZE == 0,
              "FEV_DEFAULT_GUARD_SIZE must be multiple of FEV_PAGE_SIZE");

const struct fev_fiber_attr fev_fiber_create_default_attr = {
    .stack_addr = NULL,
    .stack_size = FEV_DEFAULT_STACK_SIZE,
    .guard_size = FEV_DEFAULT_GUARD_SIZE,
    .detached = false,
};

const struct fev_fiber_attr fev_fiber_spawn_default_attr = {
    .stack_addr = NULL,
    .stack_size = FEV_DEFAULT_STACK_SIZE,
    .guard_size = FEV_DEFAULT_GUARD_SIZE,
    .detached = true,
};

FEV_NONNULL(1) int fev_fiber_attr_create(struct fev_fiber_attr **attr_ptr)
{
  struct fev_fiber_attr *attr;

  attr = fev_malloc(sizeof(*attr));
  if (FEV_UNLIKELY(attr == NULL))
    return -ENOMEM;

  /*
   * Zero-initialize the padding to not leak any information. In C11 no guarantee is given for the
   * bits of the padding of fev_fiber_create_default_attr, thus a memcpy is not enough.
   */
  memset(attr, 0, sizeof(*attr));
  attr->stack_addr = fev_fiber_create_default_attr.stack_addr;
  attr->stack_size = fev_fiber_create_default_attr.stack_size;
  attr->guard_size = fev_fiber_create_default_attr.guard_size;
  attr->detached = fev_fiber_create_default_attr.detached;

  *attr_ptr = attr;
  return 0;
}

FEV_NONNULL(1) void fev_fiber_attr_destroy(struct fev_fiber_attr *attr) { fev_free(attr); }

FEV_NONNULL(1, 2, 3)
void fev_fiber_attr_get_stack(const struct fev_fiber_attr *attr, void **stack_addr,
                              size_t *stack_size)
{
  *stack_addr = attr->stack_addr;
  *stack_size = attr->stack_size;
}

FEV_NONNULL(1, 2)
int fev_fiber_attr_set_stack(struct fev_fiber_attr *attr, void *stack_addr, size_t stack_size)
{
  if (FEV_UNLIKELY((uintptr_t)stack_addr % FEV_PAGE_SIZE != 0))
    return -EINVAL;

  if (FEV_UNLIKELY(stack_size % FEV_PAGE_SIZE != 0))
    return -EINVAL;

  attr->stack_addr = stack_addr;
  attr->stack_size = stack_size;
  return 0;
}

FEV_NONNULL(1) FEV_PURE size_t fev_fiber_attr_get_stack_size(const struct fev_fiber_attr *attr)
{
  return attr->stack_size;
}

FEV_NONNULL(1) int fev_fiber_attr_set_stack_size(struct fev_fiber_attr *attr, size_t stack_size)
{
  if (FEV_UNLIKELY(stack_size % FEV_PAGE_SIZE != 0))
    return -EINVAL;

  attr->stack_size = stack_size;
  return 0;
}

FEV_NONNULL(1) FEV_PURE size_t fev_fiber_attr_get_guard_size(const struct fev_fiber_attr *attr)
{
  return attr->guard_size;
}

FEV_NONNULL(1) int fev_fiber_attr_set_guard_size(struct fev_fiber_attr *attr, size_t guard_size)
{
  if (FEV_UNLIKELY(guard_size % FEV_PAGE_SIZE != 0))
    return -EINVAL;

  attr->guard_size = guard_size;
  return 0;
}

FEV_NONNULL(1) FEV_PURE bool fev_fiber_attr_get_detached(const struct fev_fiber_attr *attr)
{
  return attr->detached;
}

FEV_NONNULL(1) void fev_fiber_attr_set_detached(struct fev_fiber_attr *attr, bool detached)
{
  attr->detached = detached;
}
