#include <time.h>

#include <fev/fev.h>

#include "util.h"

static void *test(void *arg)
{
  (void)arg;

  for (int i = 0; i < 10; i++) {
    struct timespec now, rel_time, expected;

    clock_gettime(CLOCK_MONOTONIC, &now);

    rel_time.tv_sec = 0;
    rel_time.tv_nsec = i * 1000000;

    expected = now;
    expected.tv_sec +=
        rel_time.tv_sec + (expected.tv_nsec + rel_time.tv_nsec) / (1000 * 1000 * 1000);
    expected.tv_nsec = (expected.tv_nsec + rel_time.tv_nsec) % (1000 * 1000 * 1000);

    fev_sleep_for(&rel_time);

    clock_gettime(CLOCK_MONOTONIC, &now);
    CHECK(now.tv_sec > expected.tv_sec ||
              (now.tv_sec == expected.tv_sec && now.tv_nsec >= expected.tv_nsec),
          "Timer expired too soon");
  }

  return NULL;
}

int main(void)
{
  struct fev_sched *sched;
  int err;

  err = fev_sched_create(&sched, NULL);
  CHECK(err == 0, "Creating scheduler failed: err=%i", err);

  err = fev_fiber_spawn(sched, &test, NULL);
  CHECK(err == 0, "Creating fiber failed: err=%i", err);

  err = fev_sched_run(sched);
  CHECK(err == 0, "Running scheduler failed: err=%i", err);

  fev_sched_destroy(sched);

  return 0;
}
