/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_SOCKET_IO_URING_H
#define FEV_SOCKET_IO_URING_H

#include "fev_fiber.h"

struct fev_socket_end {
  struct fev_fiber *fiber;

  /* Number of entries still in uring for this socket. */
  unsigned num_entries;

  /* Result from uring. */
  int res;
};

struct fev_socket {
  int fd;
  struct fev_socket_end read_end;
  struct fev_socket_end write_end;
};

#endif /* !FEV_SOCKET_IO_URING_H */
