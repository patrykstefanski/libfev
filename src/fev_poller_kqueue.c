/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#include "fev_poller.h"

#include <errno.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/event.h>
#include <unistd.h>

#include <queue.h>

#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_fiber.h"
#include "fev_qsbr.h"
#include "fev_sched_impl.h"
#include "fev_socket.h"
#include "fev_time.h"
#include "fev_timers.h"
#include "fev_util.h"
#include "fev_waiter_impl.h"

FEV_NONNULL(1, 2)
int fev_poller_register(const struct fev_sched_worker *worker, struct fev_socket *socket,
                        enum fev_poller_flag flag)
{
  struct kevent event;
  uintptr_t ident;
  int ret;

  FEV_ASSERT((int)flag == EVFILT_READ || (int)flag == EVFILT_WRITE);

  FEV_ASSERT(socket->fd >= 0);
  ident = (uintptr_t)socket->fd;

  EV_SET(&event, ident, /*filter=*/flag, /*flags=*/EV_ADD | EV_CLEAR, /*fflags=*/0, /*data=*/0,
         /*udata=*/socket);
  ret = kevent(worker->poller_data.kqueue_fd, /*changelist=*/&event, /*nchanges=*/1,
               /*eventlist=*/NULL, /*nevents=*/0, /*timeout=*/NULL);
  return FEV_LIKELY(ret == 0) ? 0 : -errno;
}

FEV_NONNULL(1, 2)
void fev_poller_set_timeout(const struct fev_timers_bucket *bucket, const struct timespec *abs_time)
{
  struct timespec rel_time;
  struct kevent event;
  uint64_t ns;
  int ret;

  /*
   * It would be nice to skip the calculation of `rel_time` and use NOTE_ABSTIME, but if the
   * `abs_time` is in the past, kevent() returns EINVAL. In our case the time between e.g. struct
   * fev_socket_try_read_until() and fev_poller_set_timeout() may be long enough.
   * TODO: We could check against EINVAL and then call fev_process_timer_event().
   */

  fev_timespec_abs_to_rel(&rel_time, abs_time);
  ns = fev_timespec_to_ns(&rel_time);

  EV_SET(&event, /*ident=*/(uintptr_t)bucket, /*filter=*/EVFILT_TIMER,
         /*flags=*/EV_ADD | EV_ONESHOT | EV_CLEAR, /*fflags=*/NOTE_NSECONDS, /*data=*/ns,
         /*udata=*/NULL);
  ret = kevent(bucket->poller_data.kqueue_fd, /*changelist=*/&event, /*nchanges=*/1,
               /*eventlist=*/NULL, /*nevents=*/0, /*timeout=*/NULL);
  (void)ret;
  FEV_ASSERT(ret == 0);
}

FEV_NONNULL(1) void fev_poller_interrupt(const struct fev_poller *poller)
{
  struct kevent event;
  int ret;

  EV_SET(&event, /*ident=*/0, /*filter=*/EVFILT_USER, /*flags=*/EV_ENABLE, /*fflags=*/NOTE_TRIGGER,
         /*data=*/0, /*udata=*/NULL);
  ret = kevent(poller->kqueue_fd, /*changelist=*/&event, /*nchanges=*/1, /*eventlist=*/NULL,
               /*nevents=*/0, /*timeout=*/NULL);
  (void)ret;
  FEV_ASSERT(ret == 0);
}

FEV_NONNULL(1) static struct fev_fiber *fev_process_timer_event(struct fev_timers_bucket *bucket)
{
  struct fev_fiber *fiber = NULL;
  struct fev_timer *min;

  fev_timers_bucket_min_lock(&bucket->min_lock);

  min = bucket->min;
  if (min != NULL) {
    struct fev_waiter *waiter = min->waiter;
    enum fev_waiter_wake_result result = fev_waiter_wake(waiter, FEV_WAITER_TIMED_OUT_CHECK);
    if (result == FEV_WAITER_SET_AND_WAKE_UP)
      fiber = waiter->fiber;
  }

  fev_timers_bucket_min_unlock(&bucket->min_lock);

  return fiber;
}

FEV_NONNULL(1) static struct fev_fiber *fev_process_socket(struct fev_socket *socket, short filter)
{
  struct fev_waiter *waiter;
  enum fev_waiter_wake_result result;

  FEV_ASSERT(filter == EVFILT_READ || filter == EVFILT_WRITE);
  waiter = filter == EVFILT_READ ? &socket->read_end.waiter : &socket->write_end.waiter;

  result = fev_waiter_wake(waiter, FEV_WAITER_READY);
  return result == FEV_WAITER_SET_AND_WAKE_UP ? waiter->fiber : NULL;
}

FEV_COLD FEV_NOINLINE FEV_NORETURN static void fev_fatal_kevent(void)
{
  fputs("kevent() failed unrecoverably\n", stderr);
  abort();
}

FEV_NONNULL(1)
void fev_poller_process(struct fev_sched_worker *worker, const struct timespec *timeout)
{
  struct kevent events[FEV_POLLER_MAX_EVENTS];
  struct fev_worker_poller_data *poller_data = &worker->poller_data;
  fev_fiber_stq_head_t fibers = STAILQ_HEAD_INITIALIZER(fibers);
  uint32_t num_fibers = 0;
  int n;

  n = kevent(poller_data->kqueue_fd, /*changelist=*/NULL, /*nchanges=*/0, events,
             FEV_POLLER_MAX_EVENTS, timeout);

  if (FEV_UNLIKELY(n < 0)) {
    fev_fatal_kevent();
    FEV_UNREACHABLE();
  }

  for (int i = 0; i < n; i++) {
    struct fev_fiber *fiber;
    struct kevent *event = &events[i];

    if (FEV_UNLIKELY(event->udata == NULL)) {
      FEV_ASSERT(event->filter == EVFILT_TIMER || event->filter == EVFILT_USER);

      if (event->filter == EVFILT_USER)
        continue;

      fiber = fev_process_timer_event((void *)event->ident);
    } else {
      FEV_ASSERT(event->filter == EVFILT_READ || event->filter == EVFILT_WRITE);

      fiber = fev_process_socket(event->udata, event->filter);
    }

    if (fiber != NULL) {
      STAILQ_INSERT_TAIL(&fibers, fiber, stq_entry);
      num_fibers++;
    }
  }

  if (num_fibers > 0)
    fev_wake_stq(worker, &fibers, num_fibers);

  fev_poller_quiescent(worker);
}

FEV_COLD FEV_NONNULL(1) int fev_poller_init(struct fev_sched *sched)
{
  struct kevent event;
  struct fev_poller *poller = &sched->poller;
  struct fev_sched_worker *workers = sched->workers;
  struct fev_timers_bucket *buckets = sched->timers.buckets;
  int kqueue_fd, ret;

  kqueue_fd = kqueue();
  if (FEV_UNLIKELY(kqueue_fd < 0)) {
    ret = -errno;
    goto fail;
  }

  EV_SET(&event, /*ident=*/0, /*filter=*/EVFILT_USER, /*flags=*/EV_ADD | EV_CLEAR,
         /*fflags=*/NOTE_FFNOP, /*data=*/0, /*udata=*/NULL);
  ret = kevent(kqueue_fd, /*changelist=*/&event, /*nchanges=*/1, /*eventlist=*/NULL, /*nevents=*/0,
               /*timeout=*/NULL);
  if (FEV_UNLIKELY(ret != 0)) {
    ret = -errno;
    goto fail_kqueue;
  }

  fev_qsbr_init_global(&poller->sockets_global_qsbr, sched->num_workers);
  atomic_init(&poller->num_sockets_to_free, 0);

  for (uint32_t i = 0; i < sched->num_workers; i++) {
    struct fev_worker_poller_data *poller_data = &workers[i].poller_data;
    poller_data->kqueue_fd = kqueue_fd;
    fev_qsbr_init_local(&poller_data->sockets_local_qsbr);
  }

  /* Copy the fd to all timer buckets. */
  for (size_t i = 0; i < FEV_TIMERS_BUCKETS; i++)
    buckets[i].poller_data.kqueue_fd = kqueue_fd;

  poller->kqueue_fd = kqueue_fd;
  return 0;

fail_kqueue:
  close(kqueue_fd);

fail:
  return ret;
}

FEV_COLD FEV_NONNULL(1) void fev_poller_fini(struct fev_sched *sched)
{
  struct fev_poller *poller = &sched->poller;

  fev_poller_free_remaining_sockets(poller);
  close(sched->poller.kqueue_fd);
}
