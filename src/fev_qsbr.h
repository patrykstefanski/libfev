/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_QSBR_H
#define FEV_QSBR_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fev_assert.h"
#include "fev_compiler.h"

struct fev_qsbr_entry {
  _Atomic(struct fev_qsbr_entry *) next;
};

struct fev_qsbr_global {
  /* Current global epoch. */
  _Atomic uint32_t epoch;

  /* Number of threads that have not entered a quiescent section in the current epoch yet. */
  _Atomic uint32_t num_remaining;

  /* Entries that can be freed at current epoch + 1. */
  _Atomic(struct fev_qsbr_entry *) to_free1;

  /* Entries that can be freed at current epoch + 2. */
  _Atomic(struct fev_qsbr_entry *) to_free2;

  uint32_t num_threads;
};

struct fev_qsbr_local {
  uint32_t epoch;
};

FEV_NONNULL(1)
static inline void fev_qsbr_init_global(struct fev_qsbr_global *global, uint32_t num_threads)
{
  atomic_init(&global->epoch, 0);
  atomic_init(&global->num_remaining, 0);
  atomic_init(&global->to_free1, NULL);
  atomic_init(&global->to_free2, NULL);
  global->num_threads = num_threads;
}

FEV_NONNULL(1)
static inline void fev_qsbr_init_local(struct fev_qsbr_local *local) { local->epoch = 0; }

FEV_NONNULL(1, 2, 3)
static inline void fev_qsbr_fini_global(struct fev_qsbr_global *global,
                                        struct fev_qsbr_entry **to_free1,
                                        struct fev_qsbr_entry **to_free2)
{
  *to_free1 = atomic_load_explicit(&global->to_free1, memory_order_relaxed);
  *to_free2 = atomic_load_explicit(&global->to_free2, memory_order_relaxed);
}

FEV_NONNULL(1, 2, 3)
static inline void fev_qsbr_free(struct fev_qsbr_global *global, struct fev_qsbr_local *local,
                                 struct fev_qsbr_entry *entry)
{
  struct fev_qsbr_entry *expected = NULL, *next;
  uint32_t epoch;
  bool exchanged;

  /* qsbr_free() is not supported when the number of threads is 1. */
  FEV_ASSERT(global->num_threads > 1);

  atomic_store_explicit(&entry->next, NULL, memory_order_relaxed);
  exchanged = atomic_compare_exchange_strong_explicit(&global->to_free1, &expected, entry,
                                                      memory_order_acq_rel, memory_order_consume);
  if (FEV_UNLIKELY(exchanged)) {
    epoch = atomic_load_explicit(&global->epoch, memory_order_relaxed);
    atomic_store_explicit(&global->num_remaining, global->num_threads - 1, memory_order_relaxed);
    atomic_store_explicit(&global->epoch, epoch + 1, memory_order_release);
    local->epoch = epoch + 1;
  } else {
    next = atomic_load_explicit(&global->to_free2, memory_order_relaxed);
    atomic_store_explicit(&entry->next, next, memory_order_relaxed);
    while (!atomic_compare_exchange_weak_explicit(&global->to_free2, &entry->next, entry,
                                                  memory_order_release, memory_order_relaxed))
      ;
  }
}

FEV_NONNULL(1, 2)
FEV_WARN_UNUSED_RESULT
static inline struct fev_qsbr_entry *fev_qsbr_quiescent(struct fev_qsbr_global *global,
                                                        struct fev_qsbr_local *local)
{
  struct fev_qsbr_entry *to_free, *new_to_free1;
  uint32_t epoch, num_remaining;

  epoch = atomic_load_explicit(&global->epoch, memory_order_consume);
  if (FEV_LIKELY(epoch == local->epoch))
    return NULL;

  to_free = NULL;

  num_remaining = atomic_fetch_sub_explicit(&global->num_remaining, 1, memory_order_acq_rel);
  FEV_ASSERT(num_remaining >= 1);

  if (FEV_UNLIKELY(num_remaining == 1)) {
    to_free = atomic_load_explicit(&global->to_free1, memory_order_acquire);
    FEV_ASSERT(to_free != NULL);

    new_to_free1 = atomic_load_explicit(&global->to_free2, memory_order_consume);
    if (new_to_free1 != NULL) {
      while (!atomic_compare_exchange_weak_explicit(&global->to_free2, &new_to_free1, NULL,
                                                    memory_order_acquire, memory_order_relaxed))
        ;
      FEV_ASSERT(new_to_free1 != NULL);
      atomic_store_explicit(&global->to_free1, new_to_free1, memory_order_relaxed);
      atomic_store_explicit(&global->num_remaining, global->num_threads - 1, memory_order_relaxed);
      atomic_store_explicit(&global->epoch, epoch + 1, memory_order_release);
      epoch++;
    } else {
      atomic_store_explicit(&global->to_free1, NULL, memory_order_relaxed);
    }
  }

  local->epoch = epoch;
  return to_free;
}

#endif /* !FEV_QSBR_H */
