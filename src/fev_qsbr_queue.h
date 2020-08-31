/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_QSBR_QUEUE_H
#define FEV_QSBR_QUEUE_H

#include <fev/fev.h>

#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

#include "fev_compiler.h"
#include "fev_qsbr.h"

struct fev_qsbr_queue_node {
  struct fev_qsbr_entry qsbr_entry;
  void *value;
  _Atomic(struct fev_qsbr_queue_node *) next;
};

struct fev_qsbr_queue {
  alignas(FEV_DCACHE_LINE_SIZE) _Atomic(struct fev_qsbr_queue_node *) head;
  alignas(FEV_DCACHE_LINE_SIZE) _Atomic(struct fev_qsbr_queue_node *) tail;
};

FEV_NONNULL(1)
static inline void fev_qsbr_queue_init(struct fev_qsbr_queue *queue,
                                       struct fev_qsbr_queue_node *init_node)
{
  atomic_init(&init_node->next, NULL);
  atomic_init(&queue->head, init_node);
  atomic_init(&queue->tail, init_node);
}

FEV_NONNULL(1, 2)
static inline void fev_qsbr_queue_fini(struct fev_qsbr_queue *queue,
                                       struct fev_qsbr_queue_node **released_node)
{
  *released_node = atomic_load_explicit(&queue->head, memory_order_relaxed);
}

FEV_NONNULL(1, 2)
static inline void fev_qsbr_queue_push(struct fev_qsbr_queue *queue,
                                       struct fev_qsbr_queue_node *node, void *value)
{
  struct fev_qsbr_queue_node *tail, *next;

  node->value = value;
  atomic_store_explicit(&node->next, NULL, memory_order_relaxed);

  tail = atomic_load_explicit(&queue->tail, memory_order_relaxed);
  for (;;) {
    next = NULL;
    if (atomic_compare_exchange_weak_explicit(&tail->next, &next, node, memory_order_release,
                                              memory_order_relaxed))
      break;
    atomic_compare_exchange_weak_explicit(&queue->tail, &tail, next, memory_order_relaxed,
                                          memory_order_relaxed);
  }
  atomic_compare_exchange_strong(&queue->tail, &tail, node);
}

FEV_NONNULL(1, 2, 3)
static inline bool fev_qsbr_queue_pop(struct fev_qsbr_queue *queue,
                                      struct fev_qsbr_queue_node **released_node, void **value_ptr)
{
  struct fev_qsbr_queue_node *head, *next;

  head = atomic_load_explicit(&queue->head, memory_order_consume);
  for (;;) {
    next = atomic_load_explicit(&head->next, memory_order_acquire);
    if (next == NULL)
      return false;
    if (atomic_compare_exchange_weak_explicit(&queue->head, &head, next, memory_order_acquire,
                                              memory_order_consume))
      break;
  }

  *value_ptr = next->value;
  *released_node = head;
  return true;
}

#endif /* !FEV_QSBR_QUEUE_H */
