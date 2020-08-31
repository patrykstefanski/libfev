/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#include "fev_context.h"

#include <fev/fev.h>

#include <stdint.h>
#include <string.h>

#include "fev_assert.h"
#include "fev_compiler.h"

FEV_NONNULL(1, 2, 4)
void fev_context_init(struct fev_context *restrict context, uint8_t *restrict stack_bottom,
                      size_t stack_size, const void *restrict start_addr)
{
  uintptr_t *stack_ptr;

  FEV_ASSERT((uintptr_t)stack_bottom <= UINTPTR_MAX - stack_size);

  stack_ptr = (uintptr_t *)(stack_bottom + stack_size);
  FEV_ASSERT((uintptr_t)stack_ptr % sizeof(*stack_ptr) == 0);

  *--stack_ptr = 0xdeadbabe;
  *--stack_ptr = (uintptr_t)start_addr;

  memset(context, 0, sizeof(*context));

#if defined(FEV_ARCH_I386)
  context->esp = (uintptr_t)stack_ptr;
#elif defined(FEV_ARCH_X86_64)
  context->rsp = (uintptr_t)stack_ptr;
#endif

#if defined(FEV_ARCH_X86_64) && defined(FEV_ENABLE_ASAN)
  context->stack_bottom = stack_bottom;
  context->stack_size = stack_size;
#endif
}
