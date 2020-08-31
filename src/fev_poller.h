/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_POLLER_H
#define FEV_POLLER_H

#include <fev/fev.h>

#if defined(FEV_POLLER_EPOLL)
#include "fev_poller_epoll.h"
#include "fev_poller_reactor.h"
#elif defined(FEV_POLLER_KQUEUE)
#include "fev_poller_kqueue.h"
#include "fev_poller_reactor.h"
#elif defined(FEV_POLLER_IO_URING)
#include "fev_poller_io_uring.h"
#else
#error Wrong poller
#endif

#endif /* !FEV_POLLER_H */
