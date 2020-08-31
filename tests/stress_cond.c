#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <fev/fev.h>

#include "util.h"

static uint32_t num_fibers;
static uint32_t num_iterations;

struct fiber_data {
  struct fev_cond *cond;
  struct fev_mutex *mutex;
  uint64_t counter;
  int turn;
};

static void *work(void *arg)
{
  int turn = (uintptr_t)arg & 1;
  struct fiber_data *data = (void *)((uintptr_t)arg & ~(uintptr_t)1);

  for (uint32_t i = 0; i < num_iterations; i++) {
    fev_mutex_lock(data->mutex);

    while (data->turn != turn)
      fev_cond_wait(data->cond, data->mutex);

    data->counter++;
    data->turn = !turn;

    fev_mutex_unlock(data->mutex);

    fev_cond_notify_one(data->cond);
  }

  return NULL;
}

static void *test(void *arg)
{
  struct fiber_data *data;
  struct fev_fiber **fibers;
  uint32_t n;

  (void)arg;

  /* Well, we need at least 2 fibers... */
  n = (num_fibers + 1) / 2;

  data = malloc((size_t)n * sizeof(*data));
  CHECK(data != NULL, "Allocating memory for shared data failed");

  for (uint32_t i = 0; i < n; i++) {
    struct fiber_data *d = &data[i];
    int err;

    err = fev_cond_create(&d->cond);
    CHECK(err == 0, "Creating condition variable failed: err=%i", err);

    err = fev_mutex_create(&d->mutex);
    CHECK(err == 0, "Creating mutex failed: err=%i", err);

    d->counter = 0;
    d->turn = 1;
  }

  fibers = malloc((size_t)num_fibers * sizeof(*fibers));
  CHECK(fibers != NULL, "Allocating memory for fibers failed");

  for (uint32_t i = 0; i < n; i++) {
    void *arg = &data[i];
    int err = fev_fiber_create(&fibers[2 * i], NULL, &work, arg, NULL);
    CHECK(err == 0, "Creating fiber failed: err=%i", err);

    arg = (void *)((uintptr_t)arg | 1);
    err = fev_fiber_create(&fibers[2 * i + 1], NULL, &work, arg, NULL);
    CHECK(err == 0, "Creating fiber failed: err=%i", err);
  }

  for (uint32_t i = 0; i < num_fibers; i++)
    fev_fiber_join(fibers[i], NULL);

  free(fibers);

  for (uint32_t i = 0; i < n; i++) {
    struct fiber_data *d = &data[i];
    uint64_t expected;

    expected = (uint64_t)num_iterations * 2;
    CHECK(d->counter == expected,
          "The counter value is incorrect: counter=%" PRIu64 " expected=%" PRIu64, d->counter,
          expected);

    CHECK(d->turn == 1, "The turn is incorrect");

    fev_mutex_destroy(d->mutex);
    fev_cond_destroy(d->cond);
  }

  free(data);

  return NULL;
}

int main(int argc, char **argv)
{
  struct fev_sched_attr *sched_attr;
  struct fev_sched *sched;
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

  return 0;
}
