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
#include <fcntl.h>
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "fev_alloc.h"
#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_fiber.h"
#include "fev_poller.h"
#include "fev_sched_intf.h"
#include "fev_time.h"
#include "fev_timers.h"
#include "fev_waiter_impl.h"

static int fev_set_nonblock(int fd)
{
  int ret = ioctl(fd, FIONBIO, &(int){1});
  return FEV_LIKELY(ret == 0) ? 0 : -errno;
}

static int fev_accept_nonblock(int fd, struct sockaddr *address, socklen_t *address_len)
{
#ifdef FEV_HAVE_ACCEPT4
  int new_fd = accept4(fd, address, address_len, SOCK_NONBLOCK);
#else
  int new_fd = accept(fd, address, address_len);
#endif
  if (FEV_UNLIKELY(new_fd < 0))
    return -errno;

#ifndef FEV_HAVE_ACCEPT4
  int ret = fev_set_nonblock(new_fd);
  if (FEV_UNLIKELY(ret != 0)) {
    close(new_fd);
    return ret;
  }
#endif

  return new_fd;
}

FEV_NONNULL(1) void fev_socket_init(struct fev_socket *socket)
{
  memset(socket, 0, sizeof(*socket));
  socket->fd = -1;
}

FEV_NONNULL(1) int fev_socket_create(struct fev_socket **socket_ptr)
{
  struct fev_socket *socket;

  socket = fev_malloc(sizeof(*socket));
  if (FEV_UNLIKELY(socket == NULL))
    return -ENOMEM;

  fev_socket_init(socket);
  *socket_ptr = socket;
  return 0;
}

FEV_NONNULL(1) void fev_socket_destroy(struct fev_socket *socket)
{
  fev_poller_free_socket(fev_cur_sched_worker, socket);
}

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
  int fd, ret;

  fd = socket(domain, type, protocol);
  if (FEV_UNLIKELY(fd < 0))
    return -errno;

  ret = fev_set_nonblock(fd);
  if (FEV_UNLIKELY(ret != 0)) {
    close(fd);
    return ret;
  }

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

#define FEV_SOCKET_ACCEPT_OP                                                                       \
  int ret = fev_accept_nonblock(socket->fd, address, address_len);                                 \
  if (FEV_LIKELY(ret >= 0)) {                                                                      \
    new_socket->fd = ret;                                                                          \
    return 0;                                                                                      \
  }                                                                                                \
  err = ret;

#define FEV_SOCKET_CONNECT_OP                                                                      \
  int ret = connect(socket->fd, address, address_len);                                             \
  if (FEV_LIKELY(ret == 0))                                                                        \
    return 0;                                                                                      \
  err = errno == EINPROGRESS ? -EAGAIN : -errno;

#define FEV_SOCKET_READ_OP                                                                         \
  ssize_t n = read(socket->fd, buffer, size);                                                      \
  if (FEV_LIKELY(n >= 0))                                                                          \
    return n;                                                                                      \
  err = -errno;

#define FEV_SOCKET_WRITE_OP                                                                        \
  ssize_t n = write(socket->fd, buffer, size);                                                     \
  if (FEV_LIKELY(n >= 0))                                                                          \
    return n;                                                                                      \
  err = -errno;

#define FEV_GEN_SOCKET_OP(end, flag, op)                                                           \
  struct fev_sched_worker *cur_worker;                                                             \
  struct fev_waiter *waiter;                                                                       \
  int err;                                                                                         \
                                                                                                   \
  waiter = &(end)->waiter;                                                                         \
                                                                                                   \
  atomic_store_explicit(&waiter->reason, FEV_WAITER_NONE, memory_order_relaxed);                   \
                                                                                                   \
  op;                                                                                              \
                                                                                                   \
  if (FEV_UNLIKELY(err != -EAGAIN))                                                                \
    return err;                                                                                    \
                                                                                                   \
  cur_worker = fev_cur_sched_worker;                                                               \
  waiter->fiber = cur_worker->cur_fiber;                                                           \
                                                                                                   \
  if (FEV_UNLIKELY(!(end)->active)) {                                                              \
    err = fev_poller_register(cur_worker, socket, flag);                                           \
    if (FEV_UNLIKELY(err != 0))                                                                    \
      return err;                                                                                  \
    (end)->active = true;                                                                          \
  }                                                                                                \
                                                                                                   \
  for (;;) {                                                                                       \
    FEV_ASSERT(atomic_load(&waiter->do_wake) == 0);                                                \
                                                                                                   \
    if (FEV_UNLIKELY(socket->error != 0))                                                          \
      return -ECONNRESET;                                                                          \
                                                                                                   \
    fev_waiter_wait(waiter);                                                                       \
                                                                                                   \
    atomic_store_explicit(&waiter->reason, FEV_WAITER_NONE, memory_order_relaxed);                 \
                                                                                                   \
    op;                                                                                            \
                                                                                                   \
    if (FEV_UNLIKELY(err != -EAGAIN))                                                              \
      return err;                                                                                  \
  }

FEV_NONNULL(1, 2)
int fev_socket_accept(struct fev_socket *socket, struct fev_socket *new_socket,
                      struct sockaddr *address, socklen_t *address_len)
{
  FEV_GEN_SOCKET_OP(&socket->read_end, FEV_POLLER_IN, FEV_SOCKET_ACCEPT_OP);
}

FEV_NONNULL(1, 2)
int fev_socket_connect(struct fev_socket *socket, struct sockaddr *address, socklen_t address_len)
{
  FEV_GEN_SOCKET_OP(&socket->write_end, FEV_POLLER_OUT, FEV_SOCKET_CONNECT_OP);
}

FEV_NONNULL(1, 2)
ssize_t fev_socket_read(struct fev_socket *socket, void *buffer, size_t size)
{
  FEV_GEN_SOCKET_OP(&socket->read_end, FEV_POLLER_IN, FEV_SOCKET_READ_OP);
}

FEV_NONNULL(1, 2)
ssize_t fev_socket_write(struct fev_socket *socket, const void *buffer, size_t size)
{
  FEV_GEN_SOCKET_OP(&socket->write_end, FEV_POLLER_OUT, FEV_SOCKET_WRITE_OP);
}

#define FEV_GET_ABS_TIME                                                                           \
  struct timespec ts, *abs_time = &ts;                                                             \
  fev_get_abs_time_since_now(abs_time, rel_time);

#define FEV_GEN_SOCKET_OP_TIMEOUT(end, flag, op, time_op)                                          \
  struct fev_sched_worker *cur_worker;                                                             \
  struct fev_waiter *waiter;                                                                       \
  int err, res;                                                                                    \
                                                                                                   \
  waiter = &(end)->waiter;                                                                         \
                                                                                                   \
  atomic_store_explicit(&waiter->reason, FEV_WAITER_NONE, memory_order_relaxed);                   \
                                                                                                   \
  op;                                                                                              \
                                                                                                   \
  if (FEV_UNLIKELY(err != -EAGAIN))                                                                \
    return err;                                                                                    \
                                                                                                   \
  cur_worker = fev_cur_sched_worker;                                                               \
  waiter->fiber = cur_worker->cur_fiber;                                                           \
                                                                                                   \
  if (FEV_UNLIKELY(!(end)->active)) {                                                              \
    err = fev_poller_register(cur_worker, socket, flag);                                           \
    if (FEV_UNLIKELY(err != 0))                                                                    \
      return err;                                                                                  \
    (end)->active = true;                                                                          \
  }                                                                                                \
                                                                                                   \
  time_op;                                                                                         \
                                                                                                   \
  for (;;) {                                                                                       \
    FEV_ASSERT(atomic_load(&waiter->do_wake) == 0);                                                \
                                                                                                   \
    if (FEV_UNLIKELY(socket->error != 0))                                                          \
      return -ECONNRESET;                                                                          \
                                                                                                   \
    res = fev_timed_wait(waiter, abs_time);                                                        \
                                                                                                   \
    if (FEV_UNLIKELY(FEV_TIMED_WAIT_CAN_RETURN_ENOMEM && res == -ENOMEM))                          \
      return -ENOMEM;                                                                              \
                                                                                                   \
    if (FEV_UNLIKELY(res == -ETIMEDOUT))                                                           \
      return -ETIMEDOUT;                                                                           \
                                                                                                   \
    FEV_ASSERT(res == 0 || res == -EAGAIN);                                                        \
                                                                                                   \
    atomic_store_explicit(&waiter->reason, FEV_WAITER_NONE, memory_order_relaxed);                 \
                                                                                                   \
    op;                                                                                            \
                                                                                                   \
    if (FEV_UNLIKELY(err != -EAGAIN))                                                              \
      return err;                                                                                  \
  }

FEV_NONNULL(1, 2, 5)
int fev_socket_try_accept_until(struct fev_socket *socket, struct fev_socket *new_socket,
                                struct sockaddr *address, socklen_t *address_len,
                                const struct timespec *abs_time)
{
  FEV_GEN_SOCKET_OP_TIMEOUT(&socket->read_end, FEV_POLLER_IN, FEV_SOCKET_ACCEPT_OP, );
}

FEV_NONNULL(1, 2, 4)
int fev_socket_try_connect_until(struct fev_socket *socket, struct sockaddr *address,
                                 socklen_t address_len, const struct timespec *abs_time)
{
  FEV_GEN_SOCKET_OP_TIMEOUT(&socket->write_end, FEV_POLLER_OUT, FEV_SOCKET_CONNECT_OP, );
}

FEV_NONNULL(1, 2, 4)
ssize_t fev_socket_try_read_until(struct fev_socket *socket, void *buffer, size_t size,
                                  const struct timespec *abs_time)
{
  FEV_GEN_SOCKET_OP_TIMEOUT(&socket->read_end, FEV_POLLER_IN, FEV_SOCKET_READ_OP, );
}

FEV_NONNULL(1, 2, 4)
ssize_t fev_socket_try_write_until(struct fev_socket *socket, const void *buffer, size_t size,
                                   const struct timespec *abs_time)
{
  FEV_GEN_SOCKET_OP_TIMEOUT(&socket->write_end, FEV_POLLER_OUT, FEV_SOCKET_WRITE_OP, );
}

FEV_NONNULL(1, 2, 5)
int fev_socket_try_accept_for(struct fev_socket *socket, struct fev_socket *new_socket,
                              struct sockaddr *address, socklen_t *address_len,
                              const struct timespec *rel_time)
{
  FEV_GEN_SOCKET_OP_TIMEOUT(&socket->read_end, FEV_POLLER_IN, FEV_SOCKET_ACCEPT_OP,
                            FEV_GET_ABS_TIME);
}

FEV_NONNULL(1, 2, 4)
int fev_socket_try_connect_for(struct fev_socket *socket, struct sockaddr *address,
                               socklen_t address_len, const struct timespec *rel_time)
{
  FEV_GEN_SOCKET_OP_TIMEOUT(&socket->write_end, FEV_POLLER_OUT, FEV_SOCKET_CONNECT_OP,
                            FEV_GET_ABS_TIME);
}

FEV_NONNULL(1, 2, 4)
ssize_t fev_socket_try_read_for(struct fev_socket *socket, void *buffer, size_t size,
                                const struct timespec *rel_time)
{
  FEV_GEN_SOCKET_OP_TIMEOUT(&socket->read_end, FEV_POLLER_IN, FEV_SOCKET_READ_OP, FEV_GET_ABS_TIME);
}

FEV_NONNULL(1, 2, 4)
ssize_t fev_socket_try_write_for(struct fev_socket *socket, const void *buffer, size_t size,
                                 const struct timespec *rel_time)
{
  FEV_GEN_SOCKET_OP_TIMEOUT(&socket->write_end, FEV_POLLER_OUT, FEV_SOCKET_WRITE_OP,
                            FEV_GET_ABS_TIME);
}
