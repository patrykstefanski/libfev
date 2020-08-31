/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_CONTEXT_H
#define FEV_CONTEXT_H

#include <fev/fev.h>

#include <stddef.h>
#include <stdint.h>

#include "fev_compiler.h"

#if defined(FEV_ARCH_X86_64)

struct fev_context {
  uint32_t mxcsr;
  uint16_t fpucw;
  uint16_t _pad;
  uint64_t rsp;
  uint64_t rbp;
  uint64_t rbx;
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;

#ifdef FEV_ENABLE_ASAN
  void *fake_stack;
  const void *stack_bottom;
  size_t stack_size;
#endif
};

#elif defined(FEV_ARCH_I386)

/* TODO: Add support for ASAN. */
struct fev_context {
  uint32_t mxcsr;
  uint16_t fpucw;
  uint16_t _pad;
  uint32_t esp;
  uint32_t ebp;
  uint32_t ebx;
  uint32_t edi;
  uint32_t esi;
};

#else /* !FEV_ARCH_X86_64 && !FEV_ARCH_I386 */
#error The architecture is unsupported
#endif /* FEV_ARCH_X86_64 */

FEV_NONNULL(1, 2, 4)
void fev_context_init(struct fev_context *restrict context, uint8_t *restrict stack_bottom,
                      size_t stack_size, const void *restrict start_addr);

FEV_NONNULL(1, 2)
void fev_context_switch(struct fev_context *restrict from, struct fev_context *restrict to);

FEV_NONNULL(2, 3, 4)
void fev_context_switch_and_call(void *restrict post_arg, void *restrict post_routine,
                                 struct fev_context *restrict from,
                                 struct fev_context *restrict to);

#endif /* !FEV_CONTEXT_H */
