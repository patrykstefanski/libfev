/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#include "fev_thr_mutex_linux.h"

#include <errno.h>
#include <linux/futex.h>
#include <stdatomic.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "fev_assert.h"
#include "fev_compiler.h"

FEV_NONNULL(1) static void fev_futex_wait(int *addr, int val)
{
  long ret;

  /*
   * The wait operation returns 0 if the caller was woken up or an error:
   * EAGAIN - The value at addr was not equal to val. This is fine.
   * EINTR  - The operation was interrupted. This is fine, we don't care.
   * EINVAL - An invalid argument or inconsistency (check man pages). It's a bug
   *          in libfev, thus only an assert.
   * ENOSYS - The futex syscall is not supported. libfev's requirements assume
   *          Linux >= 2.6.26, thus only an assert.
   */
  ret = syscall(SYS_futex, addr, FUTEX_WAIT_PRIVATE, val, /*timeout=*/NULL);
  (void)ret;
  FEV_ASSERT(ret == 0 || errno == EAGAIN || errno == EINTR);
}

FEV_NONNULL(1) static void fev_futex_wake(int *addr, int val)
{
  long ret;

  /*
   * The wake operation returns the number of woken waiters or an error:
   * EFAULT - addr did not point to a valid user-space address. This is a bug
   *          in libfev, thus only an assert.
   * EINVAL - An invalid argument or an inconsistency between the user-space at
   *          addr and the kernel state, there is a waiter waiting in
   *          FUTEX_LOCK_PI. In both cases this is a bug in libfev, thus only
   *          an assert.
   * ENOSYS - The futex syscall is not supported. libfev's requirements assume
   *          Linux >= 2.6.26, thus only an assert.
   */
  ret = syscall(SYS_futex, addr, FUTEX_WAKE_PRIVATE, val);
  (void)ret;
  FEV_ASSERT(ret >= 0);
}

FEV_NONNULL(1) void fev_thr_mutex_lock_slow(struct fev_thr_mutex *mutex)
{
  int state = atomic_load_explicit(&mutex->state, memory_order_acquire);
  if (state != 2)
    state = atomic_exchange_explicit(&mutex->state, 2, memory_order_acquire);
  while (state != 0) {
    fev_futex_wait((int *)&mutex->state, 2);
    state = atomic_exchange_explicit(&mutex->state, 2, memory_order_acquire);
  }
}

FEV_NONNULL(1) void fev_thr_mutex_unlock_slow(struct fev_thr_mutex *mutex)
{
  fev_futex_wake((int *)&mutex->state, 1);
}
