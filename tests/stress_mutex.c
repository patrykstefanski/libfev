#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <fev/fev.h>

#include "util.h"

static uint32_t num_fibers;
static uint32_t num_iterations;

static struct fev_mutex *mutex;
static uint64_t counter;

static void *work(void *arg)
{
  (void)arg;

  for (uint32_t i = 0; i < num_iterations; i++) {
    fev_mutex_lock(mutex);
    counter++;
    fev_mutex_unlock(mutex);
  }

  return NULL;
}

static void *test(void *arg)
{
  struct fev_fiber **fibers;
  int err;

  (void)arg;

  err = fev_mutex_create(&mutex);
  CHECK(err == 0, "Creating mutex failed with: err=%i", err);

  fibers = malloc((size_t)num_fibers * sizeof(*fibers));
  CHECK(fibers != NULL, "Allocating memory for fibers failed");

  for (uint32_t i = 0; i < num_fibers; i++) {
    err = fev_fiber_create(&fibers[i], NULL, &work, NULL, NULL);
    CHECK(err == 0, "Creating fiber failed: err=%i", err);
  }

  for (uint32_t i = 0; i < num_fibers; i++)
    fev_fiber_join(fibers[i], NULL);

  free(fibers);

  fev_mutex_destroy(mutex);

  return NULL;
}

int main(int argc, char **argv)
{
  struct fev_sched_attr *sched_attr;
  struct fev_sched *sched;
  uint64_t expected;
  uint32_t num_workers;
  int err;

  CHECK(argc == 4, "Usage: %s <num_workers> <num_fibers> <num_iterations>", argv[0]);

  num_workers = parse_uint32_t(argv[1], "num_workers", &(uint32_t){1});
  num_fibers = parse_uint32_t(argv[2], "num_fibers", &(uint32_t){1});
  num_iterations = parse_uint32_t(argv[3], "num_iterations", &(uint32_t){1});

  err = fev_sched_attr_create(&sched_attr);
  CHECK(err == 0, "Creating scheduler attributes failed: err=%i", err);

  fev_sched_attr_set_num_workers(sched_attr, num_workers);

  err = fev_sched_create(&sched, sched_attr);
  CHECK(err == 0, "Creating scheduler failed: err=%i", err);

  fev_sched_attr_destroy(sched_attr);

  err = fev_fiber_spawn(sched, &test, NULL);
  CHECK(err == 0, "Creating fiber failed: err=%i", err);

  err = fev_sched_run(sched);
  CHECK(err == 0, "Running scheduler failed: err=%i", err);

  fev_sched_destroy(sched);

  expected = (uint64_t)num_fibers * (uint64_t)num_iterations;
  printf("counter: %" PRIu64 ", expected: %" PRIu64 "\n", counter, expected);

  return counter != expected;
}
