#include <inttypes.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../src/fev_alloc.h"
#include "../src/fev_qsbr.h"
#include "../src/fev_qsbr_queue.h"
#include "../src/fev_thr.h"
#include "../src/fev_util.h"

#include "util.h"

static uint32_t num_workers;
static uint32_t num_tries;
static struct fev_qsbr_queue queue;
static atomic_uint barrier;
static _Atomic uint64_t total_sum;
static struct fev_qsbr_global qsbr_global;

static struct fev_qsbr_queue_node *alloc_node(void)
{
  struct fev_qsbr_queue_node *node;

  node = fev_malloc(sizeof(*node));
  CHECK(node != NULL, "Allocating node failed");

  CHECK((uintptr_t)node % 8 == 0, "Bad alignment");

  return node;
}

static void free_node(struct fev_qsbr_queue_node *node) { fev_free(node); }

static void free_qsbr_nodes(struct fev_qsbr_entry *head)
{
  struct fev_qsbr_entry *next;

  while (head != NULL) {
    next = atomic_load_explicit(&head->next, memory_order_relaxed);
    free_node(FEV_CONTAINER_OF(head, struct fev_qsbr_queue_node, qsbr_entry));
    head = next;
  }
}

static void *worker_proc(void *arg)
{
  struct fev_qsbr_local qsbr_local;
  struct fev_qsbr_entry *to_free;
  uint64_t sum = 0;
  uint32_t num_enqueue = 0, num_dequeue = 0;
  uint32_t r = (uint32_t)(uintptr_t)arg;

  fev_qsbr_init_local(&qsbr_local);

  /* Wait until all threads are created. */
  atomic_fetch_sub(&barrier, 1);
  while (atomic_load_explicit(&barrier, memory_order_acquire) > 0)
    ;

  while (num_enqueue < num_tries || num_dequeue < num_tries) {
    uint32_t iters;

    r = FEV_RANDOM_NEXT(r);
    iters = r % 1024;
    while (iters-- > 0 && num_enqueue < num_tries) {
      struct fev_qsbr_queue_node *node = alloc_node();
      fev_qsbr_queue_push(&queue, node, (void *)(uintptr_t)(num_enqueue + 1));
      num_enqueue++;
    }

    r = FEV_RANDOM_NEXT(r);
    iters = r % 1024;
    while (iters-- > 0 && num_dequeue < num_tries) {
      struct fev_qsbr_queue_node *node;
      void *value;
      bool popped = fev_qsbr_queue_pop(&queue, &node, &value);
      if (!popped)
        continue;
      if (num_workers == 1)
        free_node(node);
      else
        fev_qsbr_free(&qsbr_global, &qsbr_local, &node->qsbr_entry);
      sum += (uint32_t)(uintptr_t)value;
      num_dequeue++;
    }

    to_free = fev_qsbr_quiescent(&qsbr_global, &qsbr_local);
    free_qsbr_nodes(to_free);
  }

  atomic_fetch_add(&total_sum, sum);

  return NULL;
}

int main(int argc, char **argv)
{
  struct fev_thr *threads;
  struct fev_qsbr_queue_node *node;
  struct fev_qsbr_entry *to_free1, *to_free2;
  uint64_t expected_sum;
  uint32_t seed, i;
  uint32_t r;

  CHECK(argc == 4, "Usage: %s <SEED> <NUM_WORKERS> <NUM_TRIES>", argv[0]);

  seed = parse_uint32_t(argv[1], "seed", &(uint32_t){1});
  num_workers = parse_uint32_t(argv[2], "num_workers", &(uint32_t){1});
  num_tries = parse_uint32_t(argv[3], "num_tries", NULL);

  threads = fev_malloc((size_t)num_workers * sizeof(*threads));
  if (threads == NULL) {
    fputs("Allocating memory for threads failed\n", stderr);
    return 1;
  }

  fev_qsbr_init_global(&qsbr_global, num_workers);

  node = alloc_node();
  fev_qsbr_queue_init(&queue, node);

  atomic_store(&barrier, num_workers);

  r = seed;
  for (i = 0; i < num_workers; i++) {
    r = FEV_RANDOM_NEXT(r);
    int err = fev_thr_create(&threads[i], worker_proc, (void *)(uintptr_t)r);
    if (err != 0) {
      fprintf(stderr, "Creating thread failed, err=%d\n", err);
      return 1;
    }
  }

  for (i = 0; i < num_workers; i++)
    fev_thr_join(&threads[i], NULL);

  fev_free(threads);

  fev_qsbr_queue_fini(&queue, &node);
  free_node(node);

  fev_qsbr_fini_global(&qsbr_global, &to_free1, &to_free2);
  free_qsbr_nodes(to_free1);
  free_qsbr_nodes(to_free2);

  expected_sum = ((uint64_t)num_tries * ((uint64_t)num_tries + 1) / 2) * num_workers;
  printf("sum: %" PRIu64 ", expected: %" PRIu64 "\n", atomic_load(&total_sum), expected_sum);

  return total_sum != expected_sum;
}
