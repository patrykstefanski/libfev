/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_SIMPLE_MPMC_QUEUE_H
#define FEV_SIMPLE_MPMC_QUEUE_H

#include <fev/fev.h>

#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fev_arch.h"
#include "fev_compiler.h"

/*
 * A simple lock-free queue, based on:
 * M. M. Michael and M. L. Scott. Simple, fast, and practical non-blocking and blocking concurrent
 * queue algorithms.
 *
 * This queue needs a double-compare-and-swap instruction (e.g. cmpxchg16b on x86_64).
 *
 * Allocated nodes have to stay in the memory, otherwise a use-after-free is possible. You can use a
 * memory pool defined in fev_simple_mpmc_pool.h.
 */

struct fev_simple_mpmc_queue_node;

struct fev_simple_mpmc_queue_ptr {
  /* 16-byte alignment is necessary for fev_cmpxchg2(). */
  alignas(16) _Atomic(struct fev_simple_mpmc_queue_node *) ptr;
  atomic_uintptr_t count;
};

struct fev_simple_mpmc_queue_node {
  void *value;
  struct fev_simple_mpmc_queue_ptr next;
};

struct fev_simple_mpmc_queue {
  /* Align to cache line to avoid false sharing. */
  alignas(FEV_DCACHE_LINE_SIZE) struct fev_simple_mpmc_queue_ptr head;
  alignas(FEV_DCACHE_LINE_SIZE) struct fev_simple_mpmc_queue_ptr tail;
};

FEV_NONNULL(1, 2)
static inline void fev_simple_mpmc_queue_init(struct fev_simple_mpmc_queue *queue,
                                              struct fev_simple_mpmc_queue_node *init_node)
{
  atomic_store_explicit(&init_node->next.ptr, NULL, memory_order_relaxed);
  atomic_store_explicit(&init_node->next.count, 0, memory_order_relaxed);
  init_node->value = NULL;

  atomic_store_explicit(&queue->head.ptr, init_node, memory_order_relaxed);
  atomic_store_explicit(&queue->head.count, 0, memory_order_relaxed);

  atomic_store_explicit(&queue->tail.ptr, init_node, memory_order_relaxed);
  atomic_store_explicit(&queue->tail.count, 0, memory_order_relaxed);
}

FEV_NONNULL(1, 2)
static inline void fev_simple_mpmc_queue_fini(struct fev_simple_mpmc_queue *queue,
                                              struct fev_simple_mpmc_queue_node **freed_node)
{
  *freed_node = atomic_load_explicit(&queue->head.ptr, memory_order_relaxed);
}

FEV_NONNULL(1, 2)
static inline void fev_simple_mpmc_queue_push(struct fev_simple_mpmc_queue *queue,
                                              struct fev_simple_mpmc_queue_node *node, void *value)
{
  struct fev_simple_mpmc_queue_node *tail_ptr, *next_ptr;
  uintptr_t tail_count, next_count, count;

  node->value = value;
  atomic_store_explicit(&node->next.ptr, NULL, memory_order_relaxed);
  atomic_store_explicit(&node->next.count, 0, memory_order_release);

load_tail:
  tail_count = atomic_load_explicit(&queue->tail.count, memory_order_acquire);
  tail_ptr = atomic_load_explicit(&queue->tail.ptr, memory_order_relaxed);

load_next:
  next_count = atomic_load_explicit(&tail_ptr->next.count, memory_order_acquire);
  next_ptr = atomic_load_explicit(&tail_ptr->next.ptr, memory_order_relaxed);

  count = atomic_load_explicit(&queue->tail.count, memory_order_relaxed);
  if (FEV_UNLIKELY(count != tail_count))
    goto load_tail;

  if (FEV_UNLIKELY(next_ptr != NULL)) {
    fev_cmpxchg2(&queue->tail, &tail_ptr, &tail_count, next_ptr, tail_count + 1);
    goto load_next;
  }

  if (!fev_cmpxchg2(&tail_ptr->next, &next_ptr, &next_count, node, next_count + 1))
    goto load_tail;

  fev_cmpxchg2(&queue->tail, &tail_ptr, &tail_count, node, tail_count + 1);
}

FEV_NONNULL(1, 2, 3)
static inline bool fev_simple_mpmc_queue_pop(struct fev_simple_mpmc_queue *queue, void **value_ptr,
                                             struct fev_simple_mpmc_queue_node **freed_node)
{
  struct fev_simple_mpmc_queue_node *head_ptr, *tail_ptr, *next_ptr;
  uintptr_t head_count, tail_count;
  void *value;

load_head:
  head_count = atomic_load_explicit(&queue->head.count, memory_order_acquire);
  head_ptr = atomic_load_explicit(&queue->head.ptr, memory_order_relaxed);

load_tail:
  tail_count = atomic_load_explicit(&queue->tail.count, memory_order_acquire);
  tail_ptr = atomic_load_explicit(&queue->tail.ptr, memory_order_relaxed);

  next_ptr = atomic_load_explicit(&head_ptr->next.ptr, memory_order_relaxed);

  if (FEV_UNLIKELY(head_ptr == tail_ptr)) {
    if (FEV_LIKELY(next_ptr == NULL))
      return false;

    fev_cmpxchg2(&queue->tail, &tail_ptr, &tail_count, next_ptr, tail_count + 1);
    goto load_head;
  }

  if (FEV_UNLIKELY(next_ptr == NULL))
    goto load_head;

  value = next_ptr->value;

  if (!fev_cmpxchg2(&queue->head, &head_ptr, &head_count, next_ptr, head_count + 1))
    goto load_tail;

  *value_ptr = value;
  *freed_node = head_ptr;
  return true;
}

#endif /* !FEV_SIMPLE_MPMC_QUEUE_H */
