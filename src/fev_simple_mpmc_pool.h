/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_SIMPLE_MPMC_POOL_H
#define FEV_SIMPLE_MPMC_POOL_H

#include <stdalign.h>
#include <stddef.h>

#include "fev_alloc.h"
#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_simple_mpmc_queue.h"
#include "fev_simple_mpmc_stack.h"
#include "fev_util.h"

/*
 * A memory pool for simple mpmc queue. The pool does not return the memory to OS. It consists of a
 * local and global cache. The local cache can be used only by one thread. The global cache uses
 * mpmc stack and can be shared.
 */

struct fev_simple_mpmc_pool_elem {
  struct fev_simple_mpmc_queue_node queue_node;
  struct fev_simple_mpmc_stack_node free_elems_node;
  struct fev_simple_mpmc_pool_elem *local_next;
};

struct fev_simple_mpmc_pool_global {
  struct fev_simple_mpmc_stack free_elems;
};

struct fev_simple_mpmc_pool_local {
  struct fev_simple_mpmc_pool_elem *top;
  struct fev_simple_mpmc_pool_global *global;
  size_t max_size;
  size_t cur_size;
};

FEV_NONNULL(1)
static inline struct fev_simple_mpmc_queue_node *
fev_simple_mpmc_pool_alloc_global(struct fev_simple_mpmc_pool_global *global)
{
  struct fev_simple_mpmc_stack_node *node;
  struct fev_simple_mpmc_pool_elem *elem;

  node = fev_simple_mpmc_stack_pop(&global->free_elems);
  if (FEV_LIKELY(node != NULL))
    return &FEV_CONTAINER_OF(node, struct fev_simple_mpmc_pool_elem, free_elems_node)->queue_node;

  elem = fev_aligned_alloc(alignof(struct fev_simple_mpmc_pool_elem), sizeof(*elem));
  if (FEV_UNLIKELY(elem == NULL))
    return NULL;

  return &elem->queue_node;
}

FEV_NONNULL(1, 2)
static inline void fev_simple_mpmc_pool_free_global(struct fev_simple_mpmc_pool_global *global,
                                                    struct fev_simple_mpmc_queue_node *node)
{
  struct fev_simple_mpmc_pool_elem *elem;

  elem = FEV_CONTAINER_OF(node, struct fev_simple_mpmc_pool_elem, queue_node);
  fev_simple_mpmc_stack_push(&global->free_elems, &elem->free_elems_node);
}

FEV_NONNULL(1)
static inline void fev_simple_mpmc_pool_init_global(struct fev_simple_mpmc_pool_global *global)
{
  fev_simple_mpmc_stack_init(&global->free_elems);
}

FEV_NONNULL(1)
static inline void fev_simple_mpmc_pool_fini_global(struct fev_simple_mpmc_pool_global *global)
{
  struct fev_simple_mpmc_stack_node *node;

  while ((node = fev_simple_mpmc_stack_pop(&global->free_elems)) != NULL) {
    struct fev_simple_mpmc_pool_elem *elem;

    elem = FEV_CONTAINER_OF(node, struct fev_simple_mpmc_pool_elem, free_elems_node);
    fev_aligned_free(elem);
  }
}

FEV_NONNULL(1)
static inline struct fev_simple_mpmc_queue_node *
fev_simple_mpmc_pool_alloc_local(struct fev_simple_mpmc_pool_local *local)
{
  struct fev_simple_mpmc_pool_elem *elem;

  FEV_ASSERT(local->cur_size <= local->max_size);

  elem = local->top;

  if (FEV_UNLIKELY(elem == NULL)) {
    FEV_ASSERT(local->cur_size == 0);
    return fev_simple_mpmc_pool_alloc_global(local->global);
  }

  local->top = elem->local_next;
  --local->cur_size;
  return &elem->queue_node;
}

FEV_NONNULL(1, 2)
static inline void fev_simple_mpmc_pool_free_local(struct fev_simple_mpmc_pool_local *local,
                                                   struct fev_simple_mpmc_queue_node *node)
{
  struct fev_simple_mpmc_pool_elem *elem;

  FEV_ASSERT(local->cur_size <= local->max_size);

  if (FEV_UNLIKELY(local->cur_size == local->max_size)) {
    fev_simple_mpmc_pool_free_global(local->global, node);
    return;
  }

  elem = FEV_CONTAINER_OF(node, struct fev_simple_mpmc_pool_elem, queue_node);
  elem->local_next = local->top;
  local->top = elem;
  ++local->cur_size;
}

FEV_NONNULL(1)
static inline void fev_simple_mpmc_pool_init_local(struct fev_simple_mpmc_pool_local *local,
                                                   struct fev_simple_mpmc_pool_global *global,
                                                   size_t max_size)
{
  local->top = NULL;
  local->global = global;
  local->max_size = max_size;
  local->cur_size = 0;
}

FEV_NONNULL(1)
static inline void fev_simple_mpmc_pool_fini_local(struct fev_simple_mpmc_pool_local *local)
{
  struct fev_simple_mpmc_pool_elem *top;

  top = local->top;
  while (top != NULL) {
    struct fev_simple_mpmc_queue_node *node = &top->queue_node;
    top = top->local_next;
    fev_simple_mpmc_pool_free_global(local->global, node);
  }
}

#endif /* !FEV_SIMPLE_MPMC_POOL_H */
