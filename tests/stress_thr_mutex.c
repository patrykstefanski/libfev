#include <inttypes.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

#include "../src/fev_thr.h"
#include "../src/fev_thr_mutex.h"
#include <fev/fev.h>

#include "util.h"

static struct fev_thr_mutex mutex;
static _Atomic uint32_t barrier;
static uint32_t num_iterations;
static uint64_t counter;

static void *work(void *arg)
{
  (void)arg;

  /* Wait until all threads are created. */
  atomic_fetch_sub(&barrier, 1);
  while (atomic_load_explicit(&barrier, memory_order_acquire) > 0)
    ;

  for (uint32_t i = 0; i < num_iterations; i++) {
    fev_thr_mutex_lock(&mutex);
    counter++;
    fev_thr_mutex_unlock(&mutex);
  }

  return NULL;
}

int main(int argc, char **argv)
{
  struct fev_thr *threads;
  uint64_t expected;
  uint32_t num_threads, i;
  int err;

  CHECK(argc == 3, "Usage: %s <num_workers> <num_iterations>", argv[0]);

  num_threads = parse_uint32_t(argv[1], "num_threads", &(uint32_t){1});
  num_iterations = parse_uint32_t(argv[2], "num_iterations", NULL);

  atomic_store(&barrier, num_threads);

  threads = malloc((size_t)num_threads * sizeof(*threads));
  CHECK(threads != NULL, "Allocating memory for threads failed");

  for (i = 0; i < num_threads; i++) {
    err = fev_thr_create(&threads[i], &work, NULL);
    CHECK(err == 0, "Creating thread failed, err=%d", err);
  }

  for (i = 0; i < num_threads; i++)
    fev_thr_join(&threads[i], NULL);

  free(threads);

  expected = (uint64_t)num_threads * num_iterations;
  printf("counter: %" PRIu64 ", expected: %" PRIu64 "\n", counter, expected);

  return counter != expected;
}
