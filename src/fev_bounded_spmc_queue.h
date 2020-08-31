/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_BOUNDED_SPMC_QUEUE_H
#define FEV_BOUNDED_SPMC_QUEUE_H

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
 * A single-producer-multiple-consumer queue. Based on (which, according to the author, is based on
 * Go):
 * https://tokio.rs/blog/2019-10-scheduler#a-better-run-queue
 */

struct fev_bounded_spmc_queue {
  alignas(FEV_DCACHE_LINE_SIZE) void **buffer;
  uint32_t buffer_mask;
  alignas(FEV_DCACHE_LINE_SIZE) _Atomic uint32_t head;
  alignas(FEV_DCACHE_LINE_SIZE) _Atomic uint32_t tail;
};

FEV_NONNULL(1)
FEV_WARN_UNUSED_RESULT
static inline int fev_bounded_spmc_queue_init(struct fev_bounded_spmc_queue *queue, uint32_t size)
{
  void **buffer;

  FEV_ASSERT(size >= 2 && (size & (size - 1)) == 0);

  buffer = fev_aligned_alloc(alignof(struct fev_bounded_spmc_queue), size * sizeof(*buffer));
  if (FEV_UNLIKELY(buffer == NULL))
    return -ENOMEM;

  queue->buffer = buffer;
  queue->buffer_mask = size - 1;
  atomic_init(&queue->head, 0);
  atomic_init(&queue->tail, 0);
  return 0;
}

FEV_NONNULL(1) static inline void fev_bounded_spmc_queue_fini(struct fev_bounded_spmc_queue *queue)
{
  fev_aligned_free(queue->buffer);
}

FEV_NONNULL(1)
static inline uint32_t fev_bounded_spmc_queue_size(struct fev_bounded_spmc_queue *queue)
{
  uint32_t head, tail;

  head = atomic_load_explicit(&queue->head, memory_order_acquire);
  tail = atomic_load_explicit(&queue->tail, memory_order_relaxed);

  return tail - head;
}

FEV_NONNULL(1, 2)
FEV_WARN_UNUSED_RESULT
static inline bool fev_bounded_spmc_queue_pop(struct fev_bounded_spmc_queue *queue, void **data_ptr)
{
  void **buffer = queue->buffer;
  uint32_t buffer_mask = queue->buffer_mask, head;
  void *data;

  head = atomic_load_explicit(&queue->head, memory_order_acquire);
  for (;;) {
    uint32_t tail, index;

    tail = atomic_load_explicit(&queue->tail, memory_order_relaxed);

    if (head == tail)
      return false;

    index = head & buffer_mask;
    data = buffer[index];

    if (atomic_compare_exchange_weak_explicit(&queue->head, &head, head + 1, memory_order_release,
                                              memory_order_relaxed))
      break;
  }

  *data_ptr = data;
  return true;
}

FEV_NONNULL(1)
FEV_WARN_UNUSED_RESULT
static inline bool fev_bounded_spmc_queue_push(struct fev_bounded_spmc_queue *queue, void *data)
{
  const uint32_t buffer_mask = queue->buffer_mask;
  uint32_t head, tail;

  head = atomic_load_explicit(&queue->head, memory_order_acquire);
  tail = atomic_load_explicit(&queue->tail, memory_order_relaxed);

  if (FEV_LIKELY(tail - head <= buffer_mask)) {
    uint32_t index = tail & buffer_mask;
    queue->buffer[index] = data;
    atomic_store_explicit(&queue->tail, tail + 1, memory_order_release);
    return true;
  }

  return false;
}

FEV_NONNULL(1, 2, 3)
static inline void fev_bounded_spmc_queue_push_stq(struct fev_bounded_spmc_queue *queue,
                                                   fev_fiber_stq_head_t *stqh, uint32_t *num_fibers)
{
  void **buffer = queue->buffer;
  const uint32_t buffer_mask = queue->buffer_mask;
  uint32_t n = *num_fibers, size, head, tail, free;
  struct fev_fiber *cur;

  size = buffer_mask + 1;

  head = atomic_load_explicit(&queue->head, memory_order_acquire);
  tail = atomic_load_explicit(&queue->tail, memory_order_relaxed);

  free = size - (tail - head);
  if (free < n)
    n = free;

  cur = STAILQ_FIRST(stqh);
  for (uint32_t i = 0; i < n; i++) {
    buffer[tail++ & buffer_mask] = cur;
    cur = STAILQ_NEXT(cur, stq_entry);
  }

  /* cur should be null if and only if all fibers were pushed. */
  FEV_ASSERT((cur == NULL) == (n == *num_fibers));

  atomic_store_explicit(&queue->tail, tail, memory_order_release);

  if (cur != NULL) {
    stqh->stqh_first = cur;
  } else {
    STAILQ_INIT(stqh);
  }

  *num_fibers = n;
}

#endif /* !FEV_BOUNDED_SPMC_QUEUE_H */
