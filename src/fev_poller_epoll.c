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
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
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
  struct epoll_event event;
  unsigned old_flags, new_flags;
  int op, ret;

  FEV_ASSERT((int)flag == EPOLLIN || (int)flag == EPOLLOUT);

  old_flags = 0;
  if (socket->read_end.active)
    old_flags |= EPOLLIN;
  if (socket->write_end.active)
    old_flags |= EPOLLOUT;

  FEV_ASSERT((old_flags & flag) == 0);
  new_flags = old_flags | flag;

  op = old_flags == 0 ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;

  event.events = EPOLLRDHUP | EPOLLHUP | EPOLLET | new_flags;
  event.data.ptr = socket;

  ret = epoll_ctl(worker->poller_data.epoll_fd, op, socket->fd, &event);
  return FEV_LIKELY(ret == 0) ? 0 : -errno;
}

FEV_NONNULL(1, 2)
void fev_poller_set_timeout(const struct fev_timers_bucket *bucket, const struct timespec *abs_time)
{
  struct itimerspec timer;
  int ret;

  fev_timespec_assert_valid(abs_time);

  timer.it_interval.tv_sec = 0;
  timer.it_interval.tv_nsec = 0;
  timer.it_value = *abs_time;

  /*
   * timerfd_settime() can fail with:
   * EINVAL    - This shouldn't happen. We assume that `abs_time` is correct.
   * ECANCELED - This can be returned if TFD_TIMER_CANCEL_ON_SET flag is used, which is not used
   *             here.
   */
  ret = timerfd_settime(bucket->poller_data.timer_fd, TFD_TIMER_ABSTIME, &timer, NULL);
  (void)ret;
  FEV_ASSERT(ret == 0);
}

FEV_NONNULL(1) void fev_poller_interrupt(const struct fev_poller *poller)
{
  const uint64_t counter = 1;
  ssize_t num_written;

  num_written = write(poller->event_fd, &counter, sizeof(counter));
  (void)num_written;
  FEV_ASSERT(num_written == sizeof(counter));
}

FEV_NONNULL(1) static struct fev_fiber *fev_process_timer_fd(void *ptr)
{
  struct fev_timers_bucket *bucket;
  struct fev_timer *min;
  struct fev_fiber *fiber = NULL;

  bucket = (struct fev_timers_bucket *)((uintptr_t)ptr ^ 1);

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

FEV_NONNULL(1, 2, 3)
static void fev_process_socket(const struct epoll_event *event, fev_fiber_stq_head_t *fibers,
                               uint32_t *num_fibers)
{
  struct fev_socket *socket = event->data.ptr;
  const uint32_t events = event->events;
  struct fev_waiter *waiter;
  enum fev_waiter_wake_result result;
  bool error;

  error = (events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) != 0;
  if (FEV_UNLIKELY(error))
    socket->error = 1;

  if ((events & EPOLLIN) != 0 || error) {
    waiter = &socket->read_end.waiter;
    result = fev_waiter_wake(waiter, FEV_WAITER_READY);
    if (result == FEV_WAITER_SET_AND_WAKE_UP) {
      STAILQ_INSERT_TAIL(fibers, waiter->fiber, stq_entry);
      (*num_fibers)++;
    }
  }

  if ((events & EPOLLOUT) != 0 || error) {
    waiter = &socket->write_end.waiter;
    result = fev_waiter_wake(waiter, FEV_WAITER_READY);
    if (result == FEV_WAITER_SET_AND_WAKE_UP) {
      STAILQ_INSERT_TAIL(fibers, waiter->fiber, stq_entry);
      (*num_fibers)++;
    }
  }
}

FEV_COLD FEV_NOINLINE FEV_NORETURN static void fev_fatal_epoll(void)
{
  fputs("epoll_wait() failed unrecoverably\n", stderr);
  abort();
}

FEV_NONNULL(1) void fev_poller_process(struct fev_sched_worker *worker, int timeout)
{
  struct epoll_event events[FEV_POLLER_MAX_EVENTS];
  struct fev_worker_poller_data *poller_data = &worker->poller_data;
  fev_fiber_stq_head_t fibers = STAILQ_HEAD_INITIALIZER(fibers);
  uint32_t num_fibers = 0;
  int n;

  n = epoll_wait(poller_data->epoll_fd, events, FEV_POLLER_MAX_EVENTS, timeout);

  if (FEV_UNLIKELY(n < 0)) {
    fev_fatal_epoll();
    FEV_UNREACHABLE();
  }

  for (int i = 0; i < n; i++) {
    void *ptr = events[i].data.ptr;

    if (FEV_UNLIKELY(ptr == NULL)) {
      /* event fd, we can ignore it, as its purpose was to wake epoll. */
      continue;
    }

    if (FEV_UNLIKELY((uintptr_t)ptr & 1)) {
      struct fev_fiber *fiber = fev_process_timer_fd(ptr);

      if (fiber != NULL) {
        STAILQ_INSERT_TAIL(&fibers, fiber, stq_entry);
        num_fibers++;
      }
    } else {
      fev_process_socket(&events[i], &fibers, &num_fibers);
    }
  }

  if (num_fibers > 0)
    fev_wake_stq(worker, &fibers, num_fibers);

  fev_poller_quiescent(worker);
}

FEV_COLD FEV_NONNULL(1, 2) static int fev_poller_create_timer_fds(const struct fev_poller *poller,
                                                                  struct fev_timers *timers)
{
  size_t n;
  int ret;

  for (n = 0; n < FEV_TIMERS_BUCKETS; n++) {
    struct epoll_event event;
    struct fev_timers_bucket *bucket;
    int fd;

    fd = timerfd_create(FEV_CLOCK_ID, TFD_CLOEXEC | TFD_NONBLOCK);
    if (FEV_UNLIKELY(fd < 0)) {
      ret = -errno;
      goto fail;
    }

    bucket = &timers->buckets[n];

    event.events = EPOLLIN | EPOLLET;
    event.data.ptr = (void *)((uintptr_t)bucket | (uintptr_t)1);
    ret = epoll_ctl(poller->epoll_fd, EPOLL_CTL_ADD, fd, &event);
    if (FEV_UNLIKELY(ret != 0)) {
      ret = -errno;
      close(fd);
      goto fail;
    }

    bucket->poller_data.timer_fd = fd;
  }

  return 0;

fail:
  while (n-- > 0) {
    struct fev_timers_bucket *bucket = &timers->buckets[n];
    close(bucket->poller_data.timer_fd);

    /*
     * There is no need to deregiser the fd from epoll, since the epoll instance
     * is going to be closed in fev_poller_init() anyway.
     */
  }

  return ret;
}

FEV_COLD FEV_NONNULL(1) int fev_poller_init(struct fev_sched *sched)
{
  struct epoll_event ev;
  struct fev_poller *poller = &sched->poller;
  int ret;

  poller->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
  if (FEV_UNLIKELY(poller->epoll_fd < 0)) {
    ret = -errno;
    goto fail;
  }

  poller->event_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (FEV_UNLIKELY(poller->event_fd < 0)) {
    ret = -errno;
    goto fail_epoll_fd;
  }

  ev.events = EPOLLIN | EPOLLET;
  ev.data.ptr = NULL;
  ret = epoll_ctl(poller->epoll_fd, EPOLL_CTL_ADD, poller->event_fd, &ev);
  if (FEV_UNLIKELY(ret != 0)) {
    ret = -errno;
    goto fail_event_fd;
  }

  ret = fev_poller_create_timer_fds(poller, &sched->timers);
  if (FEV_UNLIKELY(ret != 0))
    goto fail_event_fd;

  fev_qsbr_init_global(&poller->sockets_global_qsbr, sched->num_workers);
  atomic_init(&poller->num_sockets_to_free, 0);

  for (uint32_t i = 0; i < sched->num_workers; i++) {
    struct fev_worker_poller_data *poller_data = &sched->workers[i].poller_data;
    poller_data->epoll_fd = poller->epoll_fd;
    poller_data->event_fd = poller->event_fd;
    fev_qsbr_init_local(&poller_data->sockets_local_qsbr);
  }

  return 0;

fail_event_fd:
  close(poller->event_fd);

fail_epoll_fd:
  close(poller->epoll_fd);

fail:
  return ret;
}

FEV_COLD FEV_NONNULL(1) void fev_poller_fini(struct fev_sched *sched)
{
  struct fev_timers *timers = &sched->timers;
  struct fev_poller *poller = &sched->poller;
  struct fev_timers_bucket *bucket;

  fev_poller_free_remaining_sockets(poller);

  for (size_t i = 0; i < FEV_TIMERS_BUCKETS; i++) {
    bucket = &timers->buckets[i];
    close(bucket->poller_data.timer_fd);
  }

  close(poller->event_fd);
  close(poller->epoll_fd);
}
