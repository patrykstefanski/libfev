/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_SOCKET_REACTOR_H
#define FEV_SOCKET_REACTOR_H

#include <stdbool.h>

#include "fev_qsbr.h"
#include "fev_waiter_intf.h"

struct fev_socket_end {
  struct fev_waiter waiter;
  bool active;
};

struct fev_socket {
  struct fev_socket_end read_end;
  struct fev_socket_end write_end;
  int fd;
  unsigned error;
  struct fev_qsbr_entry qsbr_entry;
};

#endif /* !FEV_SOCKET_REACTOR_H */
