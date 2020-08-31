/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#include "fev_poller.h"

#include <fev/fev.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <linux/io_uring.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <queue.h>

#include "fev_alloc.h"
#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_fiber.h"
#include "fev_sched_impl.h"
#include "fev_socket.h"
#include "fev_time.h"
#include "fev_timers.h"
#include "fev_util.h"

static_assert((FEV_IO_URING_ENTRIES_PER_WORKER & (FEV_IO_URING_ENTRIES_PER_WORKER - 1)) == 0,
              "FEV_IO_URING_ENTRIES_PER_WORKER must be a power of 2");

FEV_NONNULL(1, 2)
FEV_NORETURN void fev_poller_set_timeout(const struct fev_timers_bucket *bucket,
                                         const struct timespec *abs_time)
{
  (void)bucket;
  (void)abs_time;

  fputs("FIXME: fev_poller_set_timeout() not implemented\n", stderr);
  abort();
}

FEV_NONNULL(1) void fev_poller_interrupt(struct fev_poller *poller)
{
  struct fev_sched *sched = FEV_CONTAINER_OF(poller, struct fev_sched, poller);
  const uint32_t num_workers = sched->num_workers;
  _Atomic(struct fev_worker_poller_data *) *waiters = poller->waiters;

  for (uint32_t i = 0; i < num_workers; i++) {
    struct fev_worker_poller_data *poller_data =
        atomic_exchange_explicit(&waiters[i], NULL, memory_order_consume);
    if (poller_data != NULL) {
      atomic_store_explicit(&poller_data->rearm_interrupt, true, memory_order_relaxed);
      ssize_t num_written = write(poller_data->event_fd, &(uint64_t){1}, sizeof(uint64_t));
      (void)num_written;
      FEV_ASSERT(num_written == sizeof(uint64_t));
      break;
    }
  }
}

FEV_NONNULL(1)
static void fev_poller_submit_eventfd(struct fev_worker_poller_data *poller_data)
{
  struct io_uring_sqe *sqe;
  int ret;
  bool ok;

  ok = fev_poller_get_sqes(poller_data, &sqe, 1);
  if (FEV_UNLIKELY(!ok))
    goto fail;

  fev_poller_make_sqe(sqe, IORING_OP_READ, /*flags=*/0, poller_data->event_fd, /*off=*/0,
                      /*addr=*/&poller_data->event_value, /*len=*/sizeof(poller_data->event_value),
                      /*user_data=*/0);

  ret = fev_poller_submit(poller_data, 1);
  if (FEV_LIKELY(ret == 1))
    return;

fail:
  fputs("FIXME: Submitting event fd failed. Try increasing FEV_IO_URING_ENTRIES_PER_WORKER\n",
        stderr);
  abort();
}

FEV_NONNULL(1) static void fev_poller_try_rearming_interrupt(struct fev_sched_worker *worker)
{
  struct fev_worker_poller_data *poller_data = &worker->poller_data;
  struct fev_sched *sched;
  ptrdiff_t index;
  bool rearm;

  rearm = atomic_load_explicit(&poller_data->rearm_interrupt, memory_order_acquire);
  if (!rearm)
    return;

  atomic_store_explicit(&poller_data->rearm_interrupt, false, memory_order_relaxed);

  sched = worker->sched;
  index = worker - sched->workers;
  FEV_ASSERT(index >= 0 && (uintptr_t)index < (uintptr_t)sched->num_workers);

  atomic_store_explicit(&sched->poller.waiters[index], poller_data, memory_order_release);

  fev_poller_submit_eventfd(poller_data);
}

FEV_NONNULL(1, 2) int fev_poller_check2(struct fev_sched_worker *worker, bool *interrupted)
{
  fev_fiber_stq_head_t fibers;
  struct fev_worker_poller_data *poller_data = &worker->poller_data;
  struct fev_poller_cqring *cqring = &poller_data->cqring;
  struct fev_socket_end *socket_end;
  uint32_t num_fibers;
  unsigned head, mask = *cqring->ring_mask;

  STAILQ_INIT(&fibers);
  num_fibers = 0;

  head = *cqring->head;

  for (;;) {
    struct io_uring_cqe *cqe;
    uint64_t user_data;
    unsigned tail;
    bool is_timeout;

    tail = *cqring->tail;

    if (head == tail)
      break;

    cqe = &cqring->cqes[head & mask];
    head++;

    user_data = cqe->user_data;

    if (FEV_UNLIKELY(user_data == 0)) {
      *interrupted = true;
      continue;
    }

    is_timeout = (user_data & (uint64_t)1) != 0;
    socket_end = (struct fev_socket_end *)(user_data & ~(uint64_t)1);

    if (!is_timeout)
      socket_end->res = cqe->res;

    if (--socket_end->num_entries == 0) {
      struct fev_fiber *fiber = socket_end->fiber;
      STAILQ_INSERT_TAIL(&fibers, fiber, stq_entry);
      num_fibers++;
    }
  }

  atomic_store_explicit((atomic_uint *)cqring->head, head, memory_order_release);

  if (num_fibers > 0)
    fev_wake_stq(worker, &fibers, num_fibers);

  FEV_ASSERT(num_fibers <= INT_MAX);
  return (int)num_fibers;
}

FEV_NONNULL(1) void fev_poller_wait(struct fev_sched_worker *worker)
{
  int ring_fd, n, ret;
  bool interrupted;

  ring_fd = worker->poller_data.ring_fd;

  for (;;) {
    interrupted = false;
    n = fev_poller_check2(worker, &interrupted);
    if (n > 0 || interrupted)
      return;

    fev_poller_try_rearming_interrupt(worker);

    ret = fev_sys_io_uring_enter(ring_fd,
                                 /*to_submit=*/0,
                                 /*min_complete=*/1,
                                 /*flags=*/IORING_ENTER_GETEVENTS,
                                 /*sig=*/NULL);
    if (FEV_UNLIKELY(ret < 0))
      goto fail;
  }

fail:
  fprintf(stderr, "io_uring_enter() failed: err=%i (%s)\n", -errno, strerror(-errno));
  abort();
}

FEV_COLD
FEV_NONNULL(1)
static int fev_poller_init_worker(struct fev_worker_poller_data *poller_data, unsigned num_entries)
{
  struct io_uring_params p = {0};
  struct fev_poller_sqring *sqring;
  struct fev_poller_cqring *cqring;
  void *sqring_ptr, *cqring_ptr, *sqes_ptr;
  size_t sqring_size, cqring_size, sqes_size;
  int ring_fd, event_fd, ret;

  /* io_uring */

  FEV_ASSERT((num_entries & (num_entries - 1)) == 0);

  ring_fd = fev_sys_io_uring_setup(num_entries, &p);
  if (FEV_UNLIKELY(ring_fd < 0)) {
    ret = -errno;
    goto fail;
  }

  /* sqring */

  sqring_size = p.sq_off.array + p.sq_entries * sizeof(unsigned);
  sqring_ptr = mmap(NULL, sqring_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, ring_fd,
                    IORING_OFF_SQ_RING);
  if (FEV_UNLIKELY(sqring_ptr == MAP_FAILED)) {
    ret = -errno;
    goto fail_ring_fd;
  }

  /* cqring */

  cqring_size = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);
  cqring_ptr = mmap(NULL, cqring_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, ring_fd,
                    IORING_OFF_CQ_RING);
  if (FEV_UNLIKELY(cqring_ptr == MAP_FAILED)) {
    ret = -errno;
    goto fail_sqring;
  }

  /* sqes */

  sqes_size = p.sq_entries * sizeof(struct io_uring_sqe);
  sqes_ptr = mmap(NULL, sqes_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, ring_fd,
                  IORING_OFF_SQES);
  if (FEV_UNLIKELY(sqes_ptr == MAP_FAILED)) {
    ret = -errno;
    goto fail_cqring;
  }

  /* event fd */

  event_fd = eventfd(0, EFD_CLOEXEC);
  if (FEV_UNLIKELY(event_fd < 0)) {
    ret = -errno;
    goto fail_sqes;
  }

  /* Fill the poller data. */

  poller_data->ring_fd = ring_fd;

  sqring = &poller_data->sqring;
  sqring->head = sqring_ptr + p.sq_off.head;
  sqring->tail = sqring_ptr + p.sq_off.tail;
  sqring->ring_mask = sqring_ptr + p.sq_off.ring_mask;
  sqring->ring_entries = sqring_ptr + p.sq_off.ring_entries;
  sqring->flags = sqring_ptr + p.sq_off.flags;
  sqring->dropped = sqring_ptr + p.sq_off.dropped;
  sqring->array = sqring_ptr + p.sq_off.array;
  sqring->sqes = sqes_ptr;

  cqring = &poller_data->cqring;
  cqring->head = cqring_ptr + p.cq_off.head;
  cqring->tail = cqring_ptr + p.cq_off.tail;
  cqring->ring_mask = cqring_ptr + p.cq_off.ring_mask;
  cqring->ring_entries = cqring_ptr + p.cq_off.ring_entries;
  cqring->overflow = cqring_ptr + p.cq_off.overflow;
  cqring->cqes = cqring_ptr + p.cq_off.cqes;

  poller_data->sqring_ptr = sqring_ptr;
  poller_data->sqring_size = sqring_size;

  poller_data->cqring_ptr = cqring_ptr;
  poller_data->cqring_size = cqring_size;

  atomic_init(&poller_data->rearm_interrupt, true);
  poller_data->event_fd = event_fd;

  return 0;

fail_sqes:
  munmap(sqes_ptr, sqes_size);

fail_cqring:
  munmap(cqring_ptr, cqring_size);

fail_sqring:
  munmap(sqring_ptr, sqring_size);

fail_ring_fd:
  close(ring_fd);

fail:
  return ret;
}

FEV_COLD
FEV_NONNULL(1) static void fev_poller_fini_worker(struct fev_worker_poller_data *poller_data)
{
  size_t size;

  size = *poller_data->sqring.ring_entries * sizeof(struct io_uring_sqe);
  munmap(poller_data->sqring.sqes, size);
  munmap(poller_data->cqring_ptr, poller_data->cqring_size);
  munmap(poller_data->sqring_ptr, poller_data->sqring_size);
  close(poller_data->ring_fd);
}

FEV_COLD FEV_NONNULL(1) int fev_poller_init(struct fev_sched *sched)
{
  struct fev_poller *poller = &sched->poller;
  struct fev_sched_worker *workers = sched->workers;
  const uint32_t num_workers = sched->num_workers;
  struct fev_worker_poller_data *poller_data;
  uint32_t n;
  int ret;

  poller->waiters = fev_malloc((size_t)num_workers * sizeof(*poller->waiters));
  if (FEV_UNLIKELY(poller->waiters == NULL))
    return -ENOMEM;

  for (uint32_t i = 0; i < num_workers; i++)
    atomic_init(&poller->waiters[i], NULL);

  for (n = 0; n < num_workers; n++) {
    poller_data = &workers[n].poller_data;

    ret = fev_poller_init_worker(poller_data, FEV_IO_URING_ENTRIES_PER_WORKER);
    if (FEV_UNLIKELY(ret != 0))
      goto fail_workers;
  }

  return 0;

fail_workers:
  while (n-- > 0) {
    poller_data = &workers[n].poller_data;
    fev_poller_fini_worker(poller_data);
  }

  fev_free(poller->waiters);
  return ret;
}

FEV_COLD FEV_NONNULL(1) void fev_poller_fini(struct fev_sched *sched)
{
  struct fev_poller *poller = &sched->poller;
  struct fev_sched_worker *workers = sched->workers;
  const uint32_t num_workers = sched->num_workers;

  for (uint32_t i = 0; i < num_workers; i++)
    fev_poller_fini_worker(&workers[i].poller_data);

  fev_free(poller->waiters);
}
