/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#include "fev_fiber.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

#include "fev_alloc.h"
#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_cond_impl.h"
#include "fev_context.h"
#include "fev_fiber_attr.h"
#include "fev_mutex_impl.h"
#include "fev_sched_impl.h"
#include "fev_stack.h"

/* Fiber's entry point. */
static void fev_fiber_start(void)
{
  struct fev_fiber *cur_fiber;
  void *ret;

  cur_fiber = fev_cur_fiber();
  FEV_ASSERT(cur_fiber != NULL);

  ret = cur_fiber->start_routine(cur_fiber->arg);
  fev_fiber_exit(ret);

  FEV_UNREACHABLE();
}

FEV_NONNULL(1, 3)
int fev_fiber_create(struct fev_fiber **fiber_ptr, struct fev_sched *sched,
                     void *(*start_routine)(void *), void *arg, const struct fev_fiber_attr *attr)
{
  struct fev_sched_worker *cur_worker;
  struct fev_fiber *fiber;
  unsigned ref_count;
  bool schedule_in_cur_worker;
  int ret;

  cur_worker = fev_cur_sched_worker;

  if (FEV_UNLIKELY(sched == NULL && cur_worker == NULL))
    return -EINVAL;

  if (sched == NULL || (cur_worker != NULL && sched == cur_worker->sched)) {
    sched = cur_worker->sched;
    schedule_in_cur_worker = true;
  } else {
    /* TODO: Currently there is no support for scheduling in other schedulers that are running. */
    if (fev_sched_is_running(sched))
      return -EINVAL;
    schedule_in_cur_worker = false;
  }

  if (attr == NULL)
    attr = &fev_fiber_create_default_attr;

  /* Joinable fibers can only be created in the same scheduler. */
  if (!schedule_in_cur_worker && !attr->detached)
    return -EINVAL;

  fiber = fev_malloc(sizeof(*fiber));
  if (FEV_UNLIKELY(fiber == NULL))
    return -ENOMEM;

  /* Use the user-specified stack or allocate a new one. */
  if (attr->stack_addr != NULL) {
    fiber->stack_addr = attr->stack_addr;
    fiber->total_stack_size = attr->stack_size;
    fiber->user_stack = true;
  } else {
    ret = fev_stack_alloc(&fiber->stack_addr, attr->stack_size, attr->guard_size);
    if (FEV_UNLIKELY(ret != 0))
      goto fail_fiber;

    fiber->total_stack_size = attr->stack_size + attr->guard_size;
    fiber->user_stack = false;
  }

  fiber->start_routine = start_routine;
  fiber->arg = arg;
  fiber->return_value = NULL;

  /* Initialize the stack and registers. */
  fev_context_init(&fiber->context, fiber->stack_addr, fiber->total_stack_size, &fev_fiber_start);

  /* Stuff for joining fiber. */

  ret = fev_cond_init(&fiber->cond);
  if (FEV_UNLIKELY(ret != 0))
    goto fail_fiber_stack;

  ret = fev_mutex_init(&fiber->mutex);
  if (FEV_UNLIKELY(ret != 0))
    goto fail_cond;

  fiber->flags = attr->detached ? 0 : FEV_FIBER_JOINABLE;

  /* Number of refs, the fiber itself + joiner (if not detached). */
  ref_count = attr->detached ? 1 : 2;
  atomic_init(&fiber->ref_count, ref_count);

  /* Necessary bookkeeping for the scheduler. */
  atomic_fetch_add_explicit(&sched->num_fibers, 1, memory_order_relaxed);

  /* Schedule the fiber. */
  if (schedule_in_cur_worker)
    fev_wake_one(cur_worker, fiber);
  else
    fev_sched_put(sched, fiber);

  *fiber_ptr = fiber;
  return 0;

fail_cond:
  fev_cond_fini(&fiber->cond);

fail_fiber_stack:
  if (!fiber->user_stack)
    fev_stack_free(fiber->stack_addr, fiber->total_stack_size);

fail_fiber:
  fev_free(fiber);

  return ret;
}

FEV_NONNULL(2)
int fev_fiber_spawn(struct fev_sched *sched, void *(*start_routine)(void *), void *arg)
{
  struct fev_fiber *fiber;
  return fev_fiber_create(&fiber, sched, start_routine, arg, &fev_fiber_spawn_default_attr);
}

FEV_NONNULL(1) static void fev_fiber_release(struct fev_fiber *fiber)
{
  unsigned ref_count;

  ref_count = atomic_fetch_sub_explicit(&fiber->ref_count, 1, memory_order_release);
  FEV_ASSERT(ref_count > 0);

  /* Free the fiber if we decreased 'ref_count' from 1 to 0. */
  if (ref_count == 1) {
    fev_mutex_fini(&fiber->mutex);
    fev_cond_fini(&fiber->cond);
    fev_free(fiber);
  }
}

FEV_NONNULL(1) static void fev_fiber_exit_post(struct fev_fiber *fiber)
{
  struct fev_sched_worker *cur_worker;
  struct fev_sched *sched;

  cur_worker = fev_cur_sched_worker;
  FEV_ASSERT(cur_worker != NULL);

  sched = cur_worker->sched;

  /* Free the stack if it was allocated in fev_fiber_create(). */
  if (!fiber->user_stack)
    fev_stack_free(fiber->stack_addr, fiber->total_stack_size);

  /* Try to free the fiber. */
  fev_fiber_release(fiber);

  /* Bookkeeping. */
  atomic_fetch_sub_explicit(&sched->num_fibers, 1, memory_order_relaxed);
  atomic_fetch_sub_explicit(&sched->num_run_fibers, 1, memory_order_relaxed);
}

FEV_NORETURN void fev_fiber_exit(void *return_value)
{
  struct fev_sched_worker *cur_worker;
  struct fev_fiber *cur_fiber;

  /* fev_fiber_exit() should be called from the fiber itself. */
  cur_worker = fev_cur_sched_worker;
  FEV_ASSERT(cur_worker != NULL);

  cur_fiber = cur_worker->cur_fiber;
  FEV_ASSERT(cur_fiber != NULL);

  /*
   * Set the return value and notify the joiner. This must be executed here, that is within a fiber
   * context, as we may have to wait for the mutex.
   */
  fev_mutex_lock(&cur_fiber->mutex);
  cur_fiber->return_value = return_value;
  cur_fiber->flags |= FEV_FIBER_DEAD;
  fev_mutex_unlock(&cur_fiber->mutex);
  fev_cond_notify_one(&cur_fiber->cond);

  /*
   * Switch to sched and execute the post operation, as here we cannot free the memory that we are
   * currently using (the fiber and its stack).
   * 'cur_worker' cannot be used here, as we could have waited for the mutex, thus the current
   * worker may be different, hence we need to reload it.
   */
  fev_context_switch_and_call(cur_fiber, &fev_fiber_exit_post, &cur_fiber->context,
                              &fev_cur_sched_worker->context);

  FEV_UNREACHABLE();
}

FEV_NONNULL(1) int fev_fiber_detach(struct fev_fiber *fiber)
{
  /*
   * fev_fiber_detach() can be only called from another fiber in the same scheduler.
   * FIXME: A check for the same scheduler is missing.
   */
  if (fev_cur_sched_worker == NULL)
    return -EINVAL;

  fev_mutex_lock(&fiber->mutex);

  if (FEV_UNLIKELY(!(fiber->flags & FEV_FIBER_JOINABLE))) {
    fev_mutex_unlock(&fiber->mutex);

    /* The fiber is not joinable. */
    return -EINVAL;
  }

  fiber->flags &= ~FEV_FIBER_JOINABLE;

  fev_mutex_unlock(&fiber->mutex);

  /* Try to free the fiber. It may be already dead at this point. */
  fev_fiber_release(fiber);

  return 0;
}

FEV_NONNULL(1) int fev_fiber_join(struct fev_fiber *fiber, void **return_value_ptr)
{
  /*
   * fev_fiber_join() can be only called from another fiber in the same scheduler.
   * FIXME: A check for the same scheduler is missing.
   */
  if (fev_cur_sched_worker == NULL)
    return -EINVAL;

  fev_mutex_lock(&fiber->mutex);

  /* Check whether another fiber is already waiting to join with this fiber. */
  if (FEV_UNLIKELY(fiber->flags & FEV_FIBER_JOINING)) {
    fev_mutex_unlock(&fiber->mutex);
    return -EINVAL;
  }
  fiber->flags |= FEV_FIBER_JOINING;

  /* Wait for the fiber. */
  for (;;) {
    /* Check whether the fiber is still joinable. */
    if (!(fiber->flags & FEV_FIBER_JOINABLE)) {
      fev_mutex_unlock(&fiber->mutex);
      return -EINVAL;
    }

    if (fiber->flags & FEV_FIBER_DEAD) {
      fev_mutex_unlock(&fiber->mutex);
      break;
    }

    fev_cond_wait(&fiber->cond, &fiber->mutex);
  }

  if (return_value_ptr != NULL)
    *return_value_ptr = fiber->return_value;

  /* Try to free the fiber. */
  fev_fiber_release(fiber);

  return 0;
}

void fev_yield(void)
{
  struct fev_sched_worker *cur_worker;
  struct fev_fiber *cur_fiber;

  cur_worker = fev_cur_sched_worker;
  FEV_ASSERT(cur_worker != NULL);

  cur_fiber = cur_worker->cur_fiber;
  FEV_ASSERT(cur_fiber != NULL);

  atomic_fetch_sub_explicit(&cur_worker->sched->num_run_fibers, 1, memory_order_relaxed);
  fev_context_switch_and_call(cur_fiber, &fev_cur_wake_one, &cur_fiber->context,
                              &cur_worker->context);
}
