/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_POLLER_EPOLL_H
#define FEV_POLLER_EPOLL_H

#include <stdatomic.h>
#include <stdint.h>
#include <sys/epoll.h>

#include "fev_compiler.h"
#include "fev_qsbr.h"
#include "fev_time.h"

struct fev_sched_worker;
struct fev_timers_bucket;
struct fev_socket;

/* The maxevents param when calling epoll_wait(). */
#ifndef FEV_POLLER_MAX_EVENTS
#define FEV_POLLER_MAX_EVENTS 64u
#endif

enum fev_poller_flag {
  FEV_POLLER_IN = EPOLLIN,
  FEV_POLLER_OUT = EPOLLOUT,
};

struct fev_poller {
  int epoll_fd;
  int event_fd;

  struct fev_qsbr_global sockets_global_qsbr;
  _Atomic uint32_t num_sockets_to_free;
};

struct fev_worker_poller_data {
  int epoll_fd;
  int event_fd;
  struct fev_qsbr_local sockets_local_qsbr;
};

struct fev_poller_timers_bucket_data {
  int timer_fd;
};

FEV_COLD FEV_NONNULL(1) int fev_poller_init(struct fev_sched *sched);

FEV_COLD FEV_NONNULL(1) void fev_poller_fini(struct fev_sched *sched);

FEV_NONNULL(1, 2)
int fev_poller_register(const struct fev_sched_worker *worker, struct fev_socket *socket,
                        enum fev_poller_flag flag);

FEV_NONNULL(1, 2)
void fev_poller_set_timeout(const struct fev_timers_bucket *bucket,
                            const struct timespec *abs_time);

FEV_NONNULL(1) void fev_poller_interrupt(const struct fev_poller *poller);

FEV_NONNULL(1) void fev_poller_process(struct fev_sched_worker *worker, int timeout);

FEV_NONNULL(1) static inline void fev_poller_check(struct fev_sched_worker *worker)
{
  /* epoll_wait() won't block. */
  fev_poller_process(worker, /*timeout=*/0);
}

FEV_NONNULL(1) static inline void fev_poller_wait(struct fev_sched_worker *worker)
{
  /* epoll_wait() will block indefinitely. */
  fev_poller_process(worker, /*timeout=*/-1);
}

#endif /* !FEV_POLLER_EPOLL_H */
