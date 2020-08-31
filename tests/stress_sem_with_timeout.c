#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../src/fev_util.h"
#include <fev/fev.h>

#include "util.h"

static uint32_t num_fibers;
static uint32_t num_iterations;
static uint64_t timeout_ns;

static struct fev_sem *sem;
static uint64_t counter;
static _Atomic uint64_t num_timeouts;

static void *work(void *arg)
{
  struct timespec rel_time;
  uint64_t timeouts = 0;
  uint32_t r;

  (void)arg;

  assert(timeout_ns <= LONG_MAX);
  rel_time.tv_sec = (time_t)timeout_ns / (1000 * 1000 * 1000);
  rel_time.tv_nsec = (long)timeout_ns % (1000 * 1000 * 1000);

  r = (uint32_t)rand();

  for (uint32_t i = 0; i < num_iterations;) {
    r = FEV_RANDOM_NEXT(r);
    if (r % 2 == 0) {
      fev_sem_wait(sem);
    } else {
      int ret = fev_sem_wait_for(sem, &rel_time);
      if (ret == -ETIMEDOUT) {
        timeouts++;
        continue;
      }
      CHECK(ret == 0, "fev_sem_wait_for() failed: err=%i", ret);
    }

    counter++;
    fev_sem_post(sem);

    i++;
  }

  atomic_fetch_add(&num_timeouts, timeouts);

  return NULL;
}

static void *test(void *arg)
{
  struct fev_fiber **fibers;
  int err;

  (void)arg;

  err = fev_sem_create(&sem, 1);
  CHECK(err == 0, "Creating sem failed with: err=%i", err);

  fibers = malloc((size_t)num_fibers * sizeof(*fibers));
  CHECK(fibers != NULL, "Allocating memory for fibers failed");

  for (uint32_t i = 0; i < num_fibers; i++) {
    err = fev_fiber_create(&fibers[i], NULL, &work, NULL, NULL);
    CHECK(err == 0, "Creating fiber failed: err=%i", err);
  }

  for (uint32_t i = 0; i < num_fibers; i++)
    fev_fiber_join(fibers[i], NULL);

  free(fibers);

  fev_sem_destroy(sem);

  return NULL;
}

int main(int argc, char **argv)
{
  struct fev_sched_attr *sched_attr;
  struct fev_sched *sched;
  uint64_t expected;
  uint32_t num_workers;
  int err;

  CHECK(argc == 5, "Usage: %s <num_workers> <num_fibers> <num_iterations> <timeout_ns>", argv[0]);

  num_workers = parse_uint32_t(argv[1], "num_workers", &(uint32_t){1});
  num_fibers = parse_uint32_t(argv[2], "num_fibers", &(uint32_t){1});
  num_iterations = parse_uint32_t(argv[3], "num_iterations", &(uint32_t){1});
  timeout_ns = parse_uint64_t(argv[4], "timeout_ns", &(uint64_t){1});

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
  printf("counter: %" PRIu64 ", expected: %" PRIu64 ", num_timeouts: %" PRIu64 "\n", counter,
         expected, atomic_load(&num_timeouts));

  return counter != expected;
}
