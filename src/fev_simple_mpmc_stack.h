/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_SIMPLE_MPMC_STACK_H
#define FEV_SIMPLE_MPMC_STACK_H

#include <stdalign.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "fev_arch.h"
#include "fev_compiler.h"

/*
 * A simple MPMC lock-free stack.
 *
 * Allocated nodes must stay in memory, otherwise an use-after-free is possible in pop() operation
 * when dereferencing top->next.
 */

struct fev_simple_mpmc_stack_node {
  _Atomic(struct fev_simple_mpmc_stack_node *) next;
};

struct fev_simple_mpmc_stack {
  /* 16-byte alignment is necessary for fev_cmpxchg2(). */
  alignas(16) _Atomic(struct fev_simple_mpmc_stack_node *) top;
  atomic_uintptr_t count;
};

FEV_NONNULL(1)
static inline void fev_simple_mpmc_stack_init(struct fev_simple_mpmc_stack *stack)
{
  atomic_init(&stack->top, NULL);
  atomic_init(&stack->count, 0);
}

FEV_NONNULL(1, 2)
static inline void fev_simple_mpmc_stack_push(struct fev_simple_mpmc_stack *stack,
                                              struct fev_simple_mpmc_stack_node *node)
{
  struct fev_simple_mpmc_stack_node *top;
  uintptr_t count;

  /* Read count before the pointer, make sure that they are not reordered. */
  count = atomic_load_explicit(&stack->count, memory_order_acquire);
  top = atomic_load_explicit(&stack->top, memory_order_relaxed);

  do {
    /*
     * node->next must be updated before top, but fev_cmpxchg2 is fully ordered, thus the store can
     * be relaxed.
     */
    atomic_store_explicit(&node->next, top, memory_order_relaxed);
  } while (!fev_cmpxchg2(stack, &top, &count, node, count + 1));
}

FEV_NONNULL(1)
static inline struct fev_simple_mpmc_stack_node *
fev_simple_mpmc_stack_pop(struct fev_simple_mpmc_stack *stack)
{
  struct fev_simple_mpmc_stack_node *top, *next;
  uintptr_t count;

  /* Read count before the pointer, make sure that they are not reordered. */
  count = atomic_load_explicit(&stack->count, memory_order_acquire);
  top = atomic_load_explicit(&stack->top, memory_order_relaxed);

  do {
    if (top == NULL)
      return NULL;
    next = atomic_load_explicit(&top->next, memory_order_relaxed);
  } while (!fev_cmpxchg2(stack, &top, &count, next, count + 1));

  return top;
}

#endif /* !FEV_SIMPLE_MPMC_STACK_H */
