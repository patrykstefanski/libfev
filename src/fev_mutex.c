/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#include "fev_mutex_impl.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fev_alloc.h"
#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_time.h"
#include "fev_waiters_queue_impl.h"

FEV_NONNULL(1) int fev_mutex_create(struct fev_mutex **mutex_ptr)
{
  struct fev_mutex *mutex;
  int ret;

  mutex = fev_malloc(sizeof(*mutex));
  if (FEV_UNLIKELY(mutex == NULL))
    return -ENOMEM;

  ret = fev_mutex_init(mutex);
  if (FEV_UNLIKELY(ret != 0)) {
    fev_free(mutex);
    return ret;
  }

  *mutex_ptr = mutex;
  return 0;
}

FEV_NONNULL(1) void fev_mutex_destroy(struct fev_mutex *mutex)
{
  fev_mutex_fini(mutex);
  fev_free(mutex);
}

FEV_NONNULL(1) bool fev_mutex_try_lock(struct fev_mutex *mutex)
{
  unsigned expected = 0, desired = 1;
  return atomic_compare_exchange_strong_explicit(&mutex->state, &expected, desired,
                                                 memory_order_acquire, memory_order_relaxed);
}

static bool fev_mutex_lock_recheck(void *arg)
{
  struct fev_mutex *mutex = arg;
  unsigned state;

  /*
   * Update the state to 2 (locked, some waiters), as we are appending a waiter in
   * fev_waiters_queue_wait().
   */
  state = atomic_exchange_explicit(&mutex->state, 2, memory_order_relaxed);
  if (state == 0) {
    /*
     * The mutex was unlocked inbetween fev_mutex_try_lock() and atomic_exchange_explicit(), update
     * the state to 1 (locked, no waiters) and let fev_waiters_queue_wait() know that we should not
     * wait.
     */
    atomic_store_explicit(&mutex->state, 1, memory_order_relaxed);
    return false;
  }

  return true;
}

FEV_NONNULL(1) void fev_mutex_lock(struct fev_mutex *mutex)
{
  int res;
  bool success;

  /* Fast path (if the mutex is not held). */
  success = fev_mutex_try_lock(mutex);
  if (FEV_LIKELY(success))
    return;

  /* Slow path. */
  res = fev_waiters_queue_wait(&mutex->wq, /*abs_time=*/NULL, &fev_mutex_lock_recheck, mutex);
  (void)res;
  FEV_ASSERT(res == 0);
}

FEV_NONNULL(1, 2)
static int fev_mutex_try_lock_until_slow(struct fev_mutex *mutex, const struct timespec *abs_time)
{
  int res;

  do {
    res = fev_waiters_queue_wait(&mutex->wq, abs_time, &fev_mutex_lock_recheck, mutex);
  } while (res == -EAGAIN);

  return res;
}

FEV_NONNULL(1, 2)
int fev_mutex_try_lock_for(struct fev_mutex *mutex, const struct timespec *rel_time)
{
  struct timespec abs_time;
  bool success;

  /* Fast path (if the mutex is not held). */
  success = fev_mutex_try_lock(mutex);
  if (FEV_LIKELY(success))
    return 0;

  /* Slow path. */
  fev_get_abs_time_since_now(&abs_time, rel_time);
  return fev_mutex_try_lock_until_slow(mutex, &abs_time);
}

FEV_NONNULL(1, 2)
int fev_mutex_try_lock_until(struct fev_mutex *mutex, const struct timespec *abs_time)
{
  bool success;

  /* Fast path (if the mutex is not held). */
  success = fev_mutex_try_lock(mutex);
  if (FEV_LIKELY(success))
    return 0;

  /* Slow path. */
  return fev_mutex_try_lock_until_slow(mutex, abs_time);
}

static void fev_mutex_unlock_callback(void *arg, uint32_t num_woken, bool is_empty)
{
  struct fev_mutex *mutex = arg;

  FEV_ASSERT(num_woken <= 1);

  if (num_woken == 0) {
    /* No waiter was woken, thus no one holds the mutex. */
    atomic_store_explicit(&mutex->state, 0, memory_order_relaxed);
  } else if (is_empty) {
    /*
     * One waiter was woken, but now the waiters queue is empty. Thus, set the state to 1 (locked,
     * no waiters).
     */
    atomic_store_explicit(&mutex->state, 1, memory_order_relaxed);
  }
}

FEV_NONNULL(1) void fev_mutex_unlock(struct fev_mutex *mutex)
{
  unsigned expected, desired;
  bool success;

  /* Fast path (if there are no waiters). */
  expected = 1;
  desired = 0;
  success = atomic_compare_exchange_strong_explicit(&mutex->state, &expected, desired,
                                                    memory_order_release, memory_order_relaxed);
  if (FEV_LIKELY(success))
    return;

  /* Slow path. */
  fev_waiters_queue_wake(&mutex->wq, /*max_waiters=*/1, &fev_mutex_unlock_callback, mutex);
}
