/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#include "fev_poller.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "fev_alloc.h"
#include "fev_compiler.h"
#include "fev_qsbr.h"
#include "fev_sched_intf.h"
#include "fev_socket.h"
#include "fev_util.h"

#define FEV_POLLER_WAKE_ALL_THRESHOLD 64
#define FEV_POLLER_WAKE_ALL_STEP 32

FEV_NONNULL(1, 2)
void fev_poller_free_socket(struct fev_sched_worker *worker, struct fev_socket *socket)
{
  struct fev_sched *sched = worker->sched;

  /*
   * The implementation of QSBR does not work if the number of threads is 1. But in that case, we
   * know there are no references to the socket, thus it can be freed now.
   */
  if (sched->num_workers == 1) {
    fev_free(socket);
  } else {
    struct fev_poller *poller = &sched->poller;
    struct fev_worker_poller_data *poller_data = &worker->poller_data;
    uint32_t num_sockets_to_free;

    fev_qsbr_free(&poller->sockets_global_qsbr, &poller_data->sockets_local_qsbr,
                  &socket->qsbr_entry);

    /*
     * Wake sleeping workers from time to time so that they can go through quiescent phase, since
     * we can only free sockets if all workers have acknowledged these frees.
     */
    num_sockets_to_free =
        atomic_fetch_add_explicit(&poller->num_sockets_to_free, 1, memory_order_relaxed);
    if (FEV_UNLIKELY(num_sockets_to_free >= FEV_POLLER_WAKE_ALL_THRESHOLD)) {
      if (num_sockets_to_free % FEV_POLLER_WAKE_ALL_STEP == 0)
        fev_sched_wake_all_workers(sched);
    }
  }
}

static inline uint32_t fev_poller_free_socket_list(struct fev_qsbr_entry *cur)
{
  uint32_t n = 0;

  while (cur != NULL) {
    struct fev_qsbr_entry *next = atomic_load_explicit(&cur->next, memory_order_relaxed);
    struct fev_socket *socket = FEV_CONTAINER_OF(cur, struct fev_socket, qsbr_entry);
    fev_free(socket);
    cur = next;
    n++;
  }

  return n;
}

FEV_NONNULL(1) void fev_poller_quiescent(struct fev_sched_worker *worker)
{
  struct fev_poller *poller = &worker->sched->poller;
  struct fev_worker_poller_data *poller_data = &worker->poller_data;
  struct fev_qsbr_entry *cur;
  uint32_t num_freed;

  /*
   * This works regardless of the number of threads. If the number of threads is 1, then the local
   * epoch will be always equal to the global epoch, and thus this will return NULL.
   */
  cur = fev_qsbr_quiescent(&poller->sockets_global_qsbr, &poller_data->sockets_local_qsbr);
  num_freed = fev_poller_free_socket_list(cur);

  if (num_freed > 0)
    atomic_fetch_sub_explicit(&poller->num_sockets_to_free, num_freed, memory_order_relaxed);
}

FEV_COLD FEV_NONNULL(1) void fev_poller_free_remaining_sockets(struct fev_poller *poller)
{
  struct fev_qsbr_entry *to_free1, *to_free2;

  fev_qsbr_fini_global(&poller->sockets_global_qsbr, &to_free1, &to_free2);
  fev_poller_free_socket_list(to_free1);
  fev_poller_free_socket_list(to_free2);

  /* We don't need to update `num_sockets_to_free`, as it won't be used anymore. */
}
