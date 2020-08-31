/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_SOCKET_H
#define FEV_SOCKET_H

#include <fev/fev.h>

#ifdef FEV_POLLER_IO_URING
#include "fev_socket_io_uring.h"
#else
#include "fev_socket_reactor.h"
#endif

#endif /* !FEV_SOCKET_H */
