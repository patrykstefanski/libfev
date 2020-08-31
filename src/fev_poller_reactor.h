/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_POLLER_REACTOR_H
#define FEV_POLLER_REACTOR_H

#include "fev_compiler.h"

struct fev_poller;
struct fev_sched_worker;
struct fev_worker_poller_data;
struct fev_socket;

/*
 * Free socket logically, physical free (via fev_free()) will be delayed to a point where nobody can
 * reference this socket.
 */
FEV_NONNULL(1, 2)
void fev_poller_free_socket(struct fev_sched_worker *worker, struct fev_socket *socket);

/*
 * Marks a state where the current worker cannot hold references to sockets. This can also
 * physically free (via fev_free()) some sockets that were previously logically freed (via
 * fev_poller_free_socket()).
 */
FEV_NONNULL(1) void fev_poller_quiescent(struct fev_sched_worker *worker);

/*
 * Free all remaining sockets that were previously logically freed regardless if anyone has any
 * references to the sockets.
 */
FEV_COLD FEV_NONNULL(1) void fev_poller_free_remaining_sockets(struct fev_poller *poller);

#endif /* !FEV_POLLER_REACTOR_H */
