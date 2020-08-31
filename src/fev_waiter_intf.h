/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_WAITER_INTF_H
#define FEV_WAITER_INTF_H

#include <stdatomic.h>
#include <stdint.h>

struct fev_fiber;

/* Result of fev_waiter_wake() */
enum fev_waiter_wake_result {
  /* Setting reason failed, possibly someone else has set it before. */
  FEV_WAITER_FAILED,

  /*
   * We managed to set wake reason, but someone else will wake up the fiber. This can happen when a
   * fiber wants to be woken up, but it is switching to scheduler right now and hasn't set `do_wake`
   * flag yet. In that case, it will be woken up just after the switch (see
   * fev_waiter_enable_wake_ups()).
   */
  FEV_WAITER_SET_ONLY,

  /* We managed to set wake reason and we are responsible of waking up the fiber. */
  FEV_WAITER_SET_AND_WAKE_UP,
};

enum fev_waiter_wake_reason {
  /* The reason is not set yet. */
  FEV_WAITER_NONE,

  /* The object is ready (e.g. an unlocked mutex, incoming data on a socket). */
  FEV_WAITER_READY,

  /*
   * The timer has expired, the caller of fev_waiter_wait() is responsible for checking the timers
   * for other timeouts. This reason is issued by the underlying poller.
   */
  FEV_WAITER_TIMED_OUT_CHECK,

  /*
   * The timer has expired, but the caller doesn't have to check for other timeouts. This is issued
   * by a fiber that is processing timers and thus we don't have to check them again.
   */
  FEV_WAITER_TIMED_OUT_NO_CHECK,
};

struct fev_waiter {
  /*
   * Reason (socket ready, timeout, etc.) that was set in attempt to wake up the fiber. The callers
   * of fev_waiter_wait() should set this to FEV_WAITER_NONE to indicate that we are ready to wait
   * for some events. Then, the event handlers will try to update it if the value is still
   * FEV_WAITER_NONE. If the handler succeeds, it will try to wake up the stored fiber.
   */
  atomic_uint reason;

  /* Must the stored fiber be woken up after setting `reason`? */
  atomic_uint do_wake;

  /*
   * Reason of wake up that is set by the worker that managed to change `do_wake` from 1 to 0. This
   * differs from `reason`, since `reason` can be updated by waiters (e.g. by fev_waiter_wait()) and
   * handlers (by fev_waiter_wake()). It is possible that `reason` is FEV_WAITER_NONE after a wake
   * up. In comparison, `wake_reason` should never be FEV_WAITER_NONE after a wake up.
   */
  atomic_uint wake_reason;

  /*
   * Should a woken up fiber wait for some operations to finish?
   * A waiter can be allocated on the stack. In such case the woken up fiber cannot return from
   * fev_waiter_wait() while procedures that hold references to the waiter are still alive,
   * otherwise stack-use-after-return bugs are possible. To overcome this problem, we will spin in
   * fev_waiter_wait() until the procedures have finished, that is when `wait_for_post` and
   * `wait_for_wake` are both 0 (or equivalently `wait` is 0).
   */
  union {
    struct {
      /* If 1, the woken fiber will wait for fev_waiter_enable_wake_ups() to finish. */
      _Atomic uint8_t wait_for_post;

      /* If 1, the woken fiber will wait for fev_waiter_wake() to finish. */
      _Atomic uint8_t wait_for_wake;
    };
    _Atomic uint16_t wait;
  };

  /* The fiber that must be woken up. */
  struct fev_fiber *fiber;
};

#endif /* !FEV_WAITER_INTF_H */
