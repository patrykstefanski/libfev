#include <inttypes.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../src/fev_alloc.h"
#include "../src/fev_simple_mpmc_pool.h"
#include "../src/fev_simple_mpmc_queue.h"
#include "../src/fev_thr.h"
#include "../src/fev_util.h"

#include "util.h"

static uint32_t num_tries;
static struct fev_simple_mpmc_queue queue;
static atomic_uint barrier;
static _Atomic uint64_t total_sum;

static struct fev_simple_mpmc_pool_global pool_global;

static void *worker_proc(void *arg)
{
  uint64_t sum = 0;
  uint32_t num_enqueue = 0, num_dequeue = 0;
  uint32_t r = (uint32_t)(uintptr_t)arg;

  struct fev_simple_mpmc_pool_local pool_local;
  fev_simple_mpmc_pool_init_local(&pool_local, &pool_global, 1024);

  /* Wait until all threads are created. */
  atomic_fetch_sub(&barrier, 1);
  while (atomic_load_explicit(&barrier, memory_order_acquire) > 0)
    ;

  while (num_enqueue < num_tries || num_dequeue < num_tries) {
    uint32_t iters;

    r = FEV_RANDOM_NEXT(r);
    iters = r % 1024;
    while (iters-- > 0 && num_enqueue < num_tries) {
      struct fev_simple_mpmc_queue_node *node = fev_simple_mpmc_pool_alloc_local(&pool_local);
      CHECK(node != NULL, "Allocating node failed");
      fev_simple_mpmc_queue_push(&queue, node, (void *)(uintptr_t)(num_enqueue + 1));
      num_enqueue++;
    }

    r = FEV_RANDOM_NEXT(r);
    iters = r % 1024;
    while (iters-- > 0 && num_dequeue < num_tries) {
      struct fev_simple_mpmc_queue_node *node;
      void *value;
      bool popped = fev_simple_mpmc_queue_pop(&queue, &value, &node);
      if (!popped)
        continue;
      fev_simple_mpmc_pool_free_local(&pool_local, node);
      sum += (uint32_t)(uintptr_t)value;
      num_dequeue++;
    }
  }

  fev_simple_mpmc_pool_fini_local(&pool_local);

  atomic_fetch_add(&total_sum, sum);

  return NULL;
}

int main(int argc, char **argv)
{
  struct fev_thr *threads;
  struct fev_simple_mpmc_queue_node *node;
  uint64_t expected_sum;
  uint32_t seed, num_workers, i;
  uint32_t r;

  CHECK(argc == 4, "Usage: %s <SEED> <NUM_WORKERS> <NUM_TRIES>", argv[0]);

  seed = parse_uint32_t(argv[1], "seed", &(uint32_t){1});
  num_workers = parse_uint32_t(argv[2], "num_workers", NULL);
  num_tries = parse_uint32_t(argv[3], "num_tries", NULL);

  threads = fev_malloc((size_t)num_workers * sizeof(*threads));
  if (threads == NULL) {
    fputs("Allocating memory for threads failed\n", stderr);
    return 1;
  }

  fev_simple_mpmc_pool_init_global(&pool_global);

  node = fev_simple_mpmc_pool_alloc_global(&pool_global);
  CHECK(node != NULL, "Allocating node failed");
  fev_simple_mpmc_queue_init(&queue, node);

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

  fev_simple_mpmc_queue_fini(&queue, &node);
  fev_simple_mpmc_pool_free_global(&pool_global, node);

  fev_simple_mpmc_pool_fini_global(&pool_global);

  expected_sum = ((uint64_t)num_tries * ((uint64_t)num_tries + 1) / 2) * num_workers;
  printf("sum: %" PRIu64 ", expected: %" PRIu64 "\n", atomic_load(&total_sum), expected_sum);

  return total_sum != expected_sum;
}
