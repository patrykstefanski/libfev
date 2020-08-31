/*
 * Copyright 2010-2011 Dmitry Vyukov
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_BOUNDED_MPMC_QUEUE_H
#define FEV_BOUNDED_MPMC_QUEUE_H

#include <fev/fev.h>

#include <errno.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <queue.h>

#include "fev_alloc.h"
#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_fiber.h"

/*
 * Based on 'Bounded MPMC queue' by Dmitry Vyukov:
 * http://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue
 */

struct fev_bounded_mpmc_queue_cell {
  _Atomic uint32_t sequence;
  void *data;
};

struct fev_bounded_mpmc_queue {
  alignas(FEV_DCACHE_LINE_SIZE) struct fev_bounded_mpmc_queue_cell *buffer;
  uint32_t buffer_mask;
  alignas(FEV_DCACHE_LINE_SIZE) _Atomic uint32_t head;
  alignas(FEV_DCACHE_LINE_SIZE) _Atomic uint32_t tail;
};

FEV_NONNULL(1)
FEV_WARN_UNUSED_RESULT
static inline int fev_bounded_mpmc_queue_init(struct fev_bounded_mpmc_queue *queue,
                                              uint32_t capacity)
{
  struct fev_bounded_mpmc_queue_cell *buffer;

  FEV_ASSERT(capacity >= 2 && (capacity & (capacity - 1)) == 0);

  buffer =
      fev_aligned_alloc(alignof(struct fev_bounded_mpmc_queue), (size_t)capacity * sizeof(*buffer));
  if (FEV_UNLIKELY(buffer == NULL))
    return -ENOMEM;

  for (uint32_t i = 0; i < capacity; i++)
    atomic_init(&buffer[i].sequence, i);

  queue->buffer = buffer;
  queue->buffer_mask = capacity - 1;
  atomic_init(&queue->head, 0);
  atomic_init(&queue->tail, 0);
  return 0;
}

FEV_NONNULL(1) static inline void fev_bounded_mpmc_queue_fini(struct fev_bounded_mpmc_queue *queue)
{
  fev_aligned_free(queue->buffer);
}

/*
 * This gives only kind of an approximation of the number of elements in the queue. It may return
 * that a queue is almost empty, but a push operation will fail. This can happen when a thread is
 * scheduled away just before the store to `&cell->sequence` in fev_bounded_mpmc_queue_pop(), but
 * other threads will pop many times and increase `head` during that time.
 */
FEV_NONNULL(1)
static inline uint32_t fev_bounded_mpmc_queue_size(struct fev_bounded_mpmc_queue *queue)
{
  uint32_t head, tail;

  head = atomic_load_explicit(&queue->head, memory_order_acquire);
  tail = atomic_load_explicit(&queue->tail, memory_order_relaxed);

  return tail - head;
}

FEV_NONNULL(1, 2)
FEV_WARN_UNUSED_RESULT
static inline bool fev_bounded_mpmc_queue_pop(struct fev_bounded_mpmc_queue *queue, void **data_ptr)
{
  struct fev_bounded_mpmc_queue_cell *buffer = queue->buffer, *cell;
  uint32_t buffer_mask = queue->buffer_mask, head;
  void *data;

  head = atomic_load_explicit(&queue->head, memory_order_relaxed);
  for (;;) {
    uint32_t seq;
    int32_t diff;

    cell = &buffer[head & buffer_mask];
    seq = atomic_load_explicit(&cell->sequence, memory_order_acquire);
    diff = (int32_t)(seq - (head + 1));
    if (diff == 0) {
      if (atomic_compare_exchange_weak_explicit(&queue->head, &head, head + 1, memory_order_relaxed,
                                                memory_order_relaxed)) {
        break;
      }
    } else if (diff < 0) {
      return false;
    } else {
      head = atomic_load_explicit(&queue->head, memory_order_relaxed);
    }
  }
  data = cell->data;
  atomic_store_explicit(&cell->sequence, head + buffer_mask + 1, memory_order_release);
  *data_ptr = data;
  return true;
}

FEV_NONNULL(1)
FEV_WARN_UNUSED_RESULT
static inline bool fev_bounded_mpmc_queue_push(struct fev_bounded_mpmc_queue *queue, void *data)
{
  struct fev_bounded_mpmc_queue_cell *buffer = queue->buffer, *cell;
  uint32_t buffer_mask = queue->buffer_mask, tail;

  tail = atomic_load_explicit(&queue->tail, memory_order_relaxed);
  for (;;) {
    uint32_t seq;
    int32_t diff;

    cell = &buffer[tail & buffer_mask];
    seq = atomic_load_explicit(&cell->sequence, memory_order_acquire);
    diff = (int32_t)(seq - tail);
    if (diff == 0) {
      if (atomic_compare_exchange_weak_explicit(&queue->tail, &tail, tail + 1, memory_order_relaxed,
                                                memory_order_relaxed)) {
        break;
      }
    } else if (diff < 0) {
      return false;
    } else {
      tail = atomic_load_explicit(&queue->tail, memory_order_relaxed);
    }
  }
  cell->data = data;
  atomic_store_explicit(&cell->sequence, tail + 1, memory_order_release);
  return true;
}

FEV_NONNULL(1, 2, 3)
static inline void fev_bounded_mpmc_queue_push_stq(struct fev_bounded_mpmc_queue *queue,
                                                   fev_fiber_stq_head_t *stqh, uint32_t *num_fibers)
{
  struct fev_fiber *cur;
  uint32_t n = 0;

  cur = STAILQ_FIRST(stqh);
  while (cur != NULL) {
    struct fev_fiber *next;
    bool pushed;

    next = STAILQ_NEXT(cur, stq_entry);
    pushed = fev_bounded_mpmc_queue_push(queue, cur);

    if (!pushed)
      break;

    cur = next;
    n++;
  }

  /* cur should be null if and only if all fibers were pushed. */
  FEV_ASSERT((cur == NULL) == (n == *num_fibers));

  if (cur != NULL) {
    stqh->stqh_first = cur;
  } else {
    STAILQ_INIT(stqh);
  }

  *num_fibers = n;
}

#endif /* !FEV_BOUNDED_MPMC_QUEUE_H */
