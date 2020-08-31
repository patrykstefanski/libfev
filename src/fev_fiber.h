/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_FIBER_H
#define FEV_FIBER_H

#include <fev/fev.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

#include <queue.h>

#include "fev_cond_intf.h"
#include "fev_context.h"
#include "fev_mutex_intf.h"

/* Fiber flags */
enum {
  /* The fiber has exited. */
  FEV_FIBER_DEAD = 1 << 0,

  /* The fiber is joinable (not detached). */
  FEV_FIBER_JOINABLE = 1 << 1,

  /* Another fiber is joining on the fiber. */
  FEV_FIBER_JOINING = 1 << 2,
};

struct fev_fiber {
  union {
    STAILQ_ENTRY(fev_fiber) stq_entry;
    TAILQ_ENTRY(fev_fiber) tq_entry;
  };

  /* Fiber's arch-specific context (registers, PC etc.). */
  struct fev_context context;

  /* Stack address and its total size (usable and guard size). */
  void *stack_addr;
  size_t total_stack_size;

  /*
   * If true, the fiber is using the user supplied stack (in 'attr' parameter). If false, the stack
   * was allocated in fev_fiber_create().
   */
  bool user_stack;

  /* The start routine of the fiber and its argument and return value. */
  void *(*start_routine)(void *);
  void *arg;
  void *return_value;

  /* Fiber flags (see above). */
  int flags;

  /* Synchronization for joining the fiber. */
  struct fev_cond cond;
  struct fev_mutex mutex;

  /* Number of refs, the fiber itself + joiner (if not detached). */
  atomic_uint ref_count;
};

typedef STAILQ_HEAD(fev_fiber_stq_head, fev_fiber) fev_fiber_stq_head_t;
typedef TAILQ_HEAD(fev_fiber_tq_head, fev_fiber) fev_fiber_tq_head_t;

#endif /* !FEV_FIBER_H */
