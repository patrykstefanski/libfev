/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#include "fev_socket.h"

#include <errno.h>
#include <linux/io_uring.h>
#include <linux/time_types.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "fev_alloc.h"
#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_context.h"
#include "fev_poller.h"
#include "fev_sched_intf.h"
#include "fev_time.h"

/*
 * TODO: Currently, when there is no sqe available, we abort the process or return -EBUSY. A
 * solution for this could be to wait on a fev_waiters_queue and retry after a wake up.
 */

FEV_NONNULL(1) void fev_socket_init(struct fev_socket *socket)
{
  memset(socket, 0, sizeof(*socket));
  socket->fd = -1;
}

FEV_NONNULL(1)
int fev_socket_create(struct fev_socket **socket_ptr)
{
  struct fev_socket *socket = fev_malloc(sizeof(*socket));
  if (FEV_UNLIKELY(socket == NULL))
    return -ENOMEM;

  fev_socket_init(socket);
  *socket_ptr = socket;
  return 0;
}

FEV_NONNULL(1) void fev_socket_destroy(struct fev_socket *socket) { fev_free(socket); }

FEV_NONNULL(1) FEV_PURE int fev_socket_native_handle(const struct fev_socket *socket)
{
  return socket->fd;
}

FEV_NONNULL(1)
int fev_socket_set_opt(struct fev_socket *socket, int level, int option_name,
                       const void *option_value, socklen_t option_len)
{
  int ret = setsockopt(socket->fd, level, option_name, option_value, option_len);
  return FEV_LIKELY(ret == 0) ? 0 : -errno;
}

FEV_NONNULL(1) int fev_socket_open(struct fev_socket *sock, int domain, int type, int protocol)
{
  int fd = socket(domain, type, protocol);
  if (FEV_UNLIKELY(fd < 0))
    return -errno;

  sock->fd = fd;
  return 0;
}

FEV_NONNULL(1) int fev_socket_close(struct fev_socket *socket)
{
  if (socket->fd != -1) {
    int ret = close(socket->fd);
    if (FEV_UNLIKELY(ret < 0))
      return -errno;
    socket->fd = -1;
  }

  return 0;
}

FEV_NONNULL(1, 2)
int fev_socket_bind(struct fev_socket *socket, const struct sockaddr *address,
                    socklen_t address_len)
{
  int ret = bind(socket->fd, address, address_len);
  return FEV_LIKELY(ret == 0) ? 0 : -errno;
}

FEV_NONNULL(1) int fev_socket_listen(struct fev_socket *socket, int backlog)
{
  int ret = listen(socket->fd, backlog);
  return FEV_LIKELY(ret == 0) ? 0 : -errno;
}

static inline int fev_socket_submit_and_wait(int fd, struct fev_socket_end *end, uint8_t opcode,
                                             uint64_t off, void *addr, uint32_t len)
{
  struct fev_sched_worker *cur_worker;
  struct fev_fiber *cur_fiber;
  struct io_uring_sqe *sqe;
  uint64_t user_data = (uint64_t)end;
  int ret;
  bool ok;

  cur_worker = fev_cur_sched_worker;
  FEV_ASSERT(cur_worker != NULL);

  cur_fiber = cur_worker->cur_fiber;
  FEV_ASSERT(cur_fiber != NULL);

  end->fiber = cur_fiber;
  end->num_entries = 1;

  ok = fev_poller_get_sqes(&cur_worker->poller_data, &sqe, 1);
  if (!ok)
    return -EBUSY;

  fev_poller_make_sqe(sqe, opcode, /*flags=*/0, fd, off, addr, len, user_data);

  ret = fev_poller_submit(&cur_worker->poller_data, 1);
  if (FEV_UNLIKELY(ret != 1)) {
    if (ret < 0)
      return ret;

    /* This should not happen. */
    abort();
  }

  // Wait.
  atomic_fetch_sub_explicit(&cur_worker->sched->num_run_fibers, 1, memory_order_relaxed);
  fev_context_switch(&cur_fiber->context, &cur_worker->context);

  FEV_ASSERT(end->num_entries == 0);

  return end->res;
}

FEV_NONNULL(1, 2)
int fev_socket_accept(struct fev_socket *socket, struct fev_socket *new_socket,
                      struct sockaddr *address, socklen_t *address_len)
{
  int fd = socket->fd;
  struct fev_socket_end *end = &socket->read_end;
  uint64_t off = (uint64_t)address_len;

  int ret = fev_socket_submit_and_wait(fd, end, IORING_OP_ACCEPT, off, address, 0);
  if (FEV_UNLIKELY(ret < 0))
    return ret;

  new_socket->fd = ret;
  return 0;
}

FEV_NONNULL(1, 2) ssize_t fev_socket_read(struct fev_socket *socket, void *buf, size_t size)
{
  int fd = socket->fd;
  struct fev_socket_end *end = &socket->read_end;
  return fev_socket_submit_and_wait(fd, end, IORING_OP_READ, 0, buf, (uint32_t)size);
}

FEV_NONNULL(1, 2) ssize_t fev_socket_write(struct fev_socket *socket, const void *buf, size_t size)
{
  int fd = socket->fd;
  struct fev_socket_end *end = &socket->write_end;
  return fev_socket_submit_and_wait(fd, end, IORING_OP_WRITE, 0, (void *)buf, (uint32_t)size);
}

static int fev_socket_submit_and_wait_for(int fd, struct fev_socket_end *end, uint8_t opcode,
                                          uint64_t off, void *addr, uint32_t len,
                                          const struct timespec *rel_time)
{
  struct fev_sched_worker *cur_worker;
  struct fev_fiber *cur_fiber;
  struct io_uring_sqe *sqes[2];
  struct __kernel_timespec ts;
  uint64_t user_data = (uint64_t)end;
  int ret;
  bool ok;

  cur_worker = fev_cur_sched_worker;
  FEV_ASSERT(cur_worker != NULL);

  cur_fiber = cur_worker->cur_fiber;
  FEV_ASSERT(cur_fiber != NULL);

  ts.tv_sec = rel_time->tv_sec;
  ts.tv_nsec = rel_time->tv_nsec;

  end->fiber = cur_fiber;
  end->num_entries = 2;

  ok = fev_poller_get_sqes(&cur_worker->poller_data, sqes, 2);
  if (!ok)
    return -EBUSY;

  fev_poller_make_sqe(sqes[0], opcode, /*flags=*/IOSQE_IO_LINK, fd, off, addr, len, user_data);

  fev_poller_make_sqe(sqes[1], IORING_OP_LINK_TIMEOUT, /*flags=*/0, /*fd=*/-1, /*off=*/0,
                      /*addr=*/&ts, /*len=*/1, user_data | 1);

  ret = fev_poller_submit(&cur_worker->poller_data, 2);
  if (FEV_UNLIKELY(ret != 2)) {
    if (ret < 0)
      return ret;

    fprintf(stderr,
            "FIXME: %i out of 2 seqs submitted. Try increasing FEV_IO_URING_ENTRIES_PER_WORKER\n",
            ret);
    abort();
  }

  // Wait.
  atomic_fetch_sub_explicit(&cur_worker->sched->num_run_fibers, 1, memory_order_relaxed);
  fev_context_switch(&cur_fiber->context, &cur_worker->context);

  FEV_ASSERT(end->num_entries == 0);

  ret = end->res;
  if (ret == -ECANCELED)
    ret = -ETIMEDOUT;
  return ret;
}

FEV_NONNULL(1, 2)
int fev_socket_try_accept_for(struct fev_socket *socket, struct fev_socket *new_socket,
                              struct sockaddr *address, socklen_t *address_len,
                              const struct timespec *rel_time)
{
  int fd = socket->fd;
  struct fev_socket_end *end = &socket->read_end;
  uint64_t off = (uint64_t)address_len;

  int ret = fev_socket_submit_and_wait_for(fd, end, IORING_OP_ACCEPT, off, address, 0, rel_time);
  if (FEV_UNLIKELY(ret < 0))
    return ret;

  new_socket->fd = ret;
  return 0;
}

FEV_NONNULL(1, 2, 4)
int fev_socket_try_connect_for(struct fev_socket *socket, struct sockaddr *address,
                               socklen_t address_len, const struct timespec *rel_time)
{
  int fd = socket->fd;
  struct fev_socket_end *end = &socket->write_end;
  uint64_t off = (uint64_t)address_len;
  return fev_socket_submit_and_wait_for(fd, end, IORING_OP_CONNECT, off, address, 0, rel_time);
}

FEV_NONNULL(1, 2, 4)
ssize_t fev_socket_try_read_for(struct fev_socket *socket, void *buffer, size_t size,
                                const struct timespec *rel_time)
{
  int fd = socket->fd;
  struct fev_socket_end *end = &socket->read_end;
  return fev_socket_submit_and_wait_for(fd, end, IORING_OP_READ, 0, buffer, (uint32_t)size,
                                        rel_time);
}

FEV_NONNULL(1, 2, 4)
ssize_t fev_socket_try_write_for(struct fev_socket *socket, const void *buffer, size_t size,
                                 const struct timespec *rel_time)
{
  int fd = socket->fd;
  struct fev_socket_end *end = &socket->write_end;
  return fev_socket_submit_and_wait_for(fd, end, IORING_OP_WRITE, 0, (void *)buffer, (uint32_t)size,
                                        rel_time);
}

FEV_NONNULL(1, 2, 5)
int fev_socket_try_accept_until(struct fev_socket *socket, struct fev_socket *new_socket,
                                struct sockaddr *address, socklen_t *address_len,
                                const struct timespec *abs_time)
{
  struct timespec rel_time;
  fev_timespec_abs_to_rel(&rel_time, abs_time);
  return fev_socket_try_accept_for(socket, new_socket, address, address_len, &rel_time);
}

FEV_NONNULL(1, 2, 4)
int fev_socket_try_connect_until(struct fev_socket *socket, struct sockaddr *address,
                                 socklen_t address_len, const struct timespec *abs_time)
{
  struct timespec rel_time;
  fev_timespec_abs_to_rel(&rel_time, abs_time);
  return fev_socket_try_connect_for(socket, address, address_len, &rel_time);
}

FEV_NONNULL(1, 2)
ssize_t fev_socket_try_read_until(struct fev_socket *socket, void *buffer, size_t size,
                                  const struct timespec *abs_time)
{
  struct timespec rel_time;
  fev_timespec_abs_to_rel(&rel_time, abs_time);
  return fev_socket_try_read_for(socket, buffer, size, &rel_time);
}

FEV_NONNULL(1, 2)
ssize_t fev_socket_try_write_until(struct fev_socket *socket, const void *buffer, size_t size,
                                   const struct timespec *abs_time)
{
  struct timespec rel_time;
  fev_timespec_abs_to_rel(&rel_time, abs_time);
  return fev_socket_try_write_for(socket, buffer, size, &rel_time);
}
