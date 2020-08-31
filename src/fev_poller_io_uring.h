/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_POLLER_IO_URING_H
#define FEV_POLLER_IO_URING_H

#include <errno.h>
#include <limits.h>
#include <linux/io_uring.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_time.h"

struct fev_sched_worker;
struct fev_timers_bucket;

struct fev_poller_sqring {
  unsigned *head;
  unsigned *tail;
  unsigned *ring_mask;
  unsigned *ring_entries;
  unsigned *flags;
  unsigned *dropped;
  unsigned *array;
  struct io_uring_sqe *sqes;
};

struct fev_poller_cqring {
  unsigned *head;
  unsigned *tail;
  unsigned *ring_mask;
  unsigned *ring_entries;
  unsigned *overflow;
  struct io_uring_cqe *cqes;
};

struct fev_worker_poller_data {
  int ring_fd;

  struct fev_poller_sqring sqring;
  struct fev_poller_cqring cqring;

  atomic_bool rearm_interrupt;
  int event_fd;
  uint64_t event_value;

  /* Additional data needed to free the io_uring instance. */
  void *sqring_ptr;
  size_t sqring_size;
  void *cqring_ptr;
  size_t cqring_size;
};

struct fev_poller {
  _Atomic(struct fev_worker_poller_data *) * waiters;
};

struct fev_poller_timers_bucket_data {
  /* FIXME: Not implemented. */
};

FEV_COLD FEV_NONNULL(1) int fev_poller_init(struct fev_sched *sched);

FEV_COLD FEV_NONNULL(1) void fev_poller_fini(struct fev_sched *sched);

FEV_NONNULL(1, 2)
void fev_poller_set_timeout(const struct fev_timers_bucket *bucket,
                            const struct timespec *abs_time);

FEV_NONNULL(1) void fev_poller_interrupt(struct fev_poller *poller);

FEV_NONNULL(1, 2) int fev_poller_check2(struct fev_sched_worker *worker, bool *interrupted);

FEV_NONNULL(1) void fev_poller_wait(struct fev_sched_worker *worker);

FEV_NONNULL(1) static inline void fev_poller_check(struct fev_sched_worker *worker)
{
  bool interrupted;
  fev_poller_check2(worker, &interrupted);
}

#ifdef __alpha__
#define FEV_SYS_io_uring_setup 535
#define FEV_SYS_io_uring_enter 536
#else
#define FEV_SYS_io_uring_setup 425
#define FEV_SYS_io_uring_enter 426
#endif

FEV_NONNULL(2)
static inline int fev_sys_io_uring_setup(unsigned entries, struct io_uring_params *params)
{
  long ret = syscall(FEV_SYS_io_uring_setup, entries, params);
  FEV_ASSERT(ret >= INT_MIN && ret <= INT_MAX);
  return (int)ret;
}

static inline int fev_sys_io_uring_enter(int fd, unsigned to_submit, unsigned min_complete,
                                         unsigned flags, sigset_t *sig)
{
  long ret = syscall(FEV_SYS_io_uring_enter, fd, to_submit, min_complete, flags, sig, _NSIG / 8);
  FEV_ASSERT(ret >= INT_MIN && ret <= INT_MAX);
  return (int)ret;
}

FEV_NONNULL(1)
static inline void fev_poller_make_sqe(struct io_uring_sqe *sqe, uint8_t opcode, uint8_t flags,
                                       int fd, uint64_t off, void *addr, uint32_t len,
                                       uint64_t user_data)
{
  sqe->opcode = opcode;
  sqe->flags = flags;
  sqe->ioprio = 0;
  sqe->fd = fd;
  sqe->off = off;
  sqe->addr = (uint64_t)addr;
  sqe->len = len;
  sqe->rw_flags = 0;
  sqe->user_data = user_data;
  sqe->__pad2[2] = sqe->__pad2[1] = sqe->__pad2[0] = 0;
}

FEV_NONNULL(1, 2)
static inline bool fev_poller_get_sqes(struct fev_worker_poller_data *poller_data,
                                       struct io_uring_sqe **sqes, unsigned num_sqes)
{
  struct fev_poller_sqring *sqring = &poller_data->sqring;
  unsigned ring_entries, tail, head, mask;

  ring_entries = *sqring->ring_entries;
  tail = *sqring->tail;
  head = atomic_load_explicit((atomic_uint *)sqring->head, memory_order_acquire);

  if (FEV_UNLIKELY(tail + num_sqes - head > ring_entries))
    return false;

  mask = *sqring->ring_mask;
  for (unsigned i = 0; i < num_sqes; i++)
    sqes[i] = &sqring->sqes[(tail + i) & mask];
  return true;
}

FEV_NONNULL(1)
static inline int fev_poller_submit(struct fev_worker_poller_data *poller_data, unsigned num_sqes)
{
  struct fev_poller_sqring *sqring = &poller_data->sqring;
  unsigned tail, mask;
  int ret;

  tail = *sqring->tail;
  mask = *sqring->ring_mask;

  for (unsigned i = 0; i < num_sqes; i++) {
    sqring->array[tail & mask] = tail & mask;
    tail++;
  }

  atomic_store_explicit((atomic_uint *)sqring->tail, tail, memory_order_release);

  ret = fev_sys_io_uring_enter(poller_data->ring_fd,
                               /*to_submit=*/num_sqes,
                               /*min_complete=*/0,
                               /*flags=*/0,
                               /*sig=*/NULL);
  return FEV_UNLIKELY(ret < 0) ? -errno : ret;
}

#endif /* !FEV_POLLER_IO_URING_H */
