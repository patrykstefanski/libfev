#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "../src/fev_alloc.h"
#include "../src/fev_time.h"
#include "../src/fev_timers.h"
#include "../src/fev_util.h"
#include <fev/fev.h>

#include "util.h"

/* TODO: More add & del min tests */

#define NUM_TIMERS 256
#define SEED 42
#define NUM_RANDOM_TRIES 1024

/* Utilities */

/*
 * If binheap implementation is used, then ptr is ignored. However, if rbtree
 * is used, then the timers must be unique and the ptr is for that reason
 * compared.
 */
static void init_timer(struct fev_timer *timer, int value, void *ptr)
{
  (void)ptr;

  timer->abs_time.tv_sec = value;
  timer->abs_time.tv_nsec = 0;

#ifdef FEV_TIMERS_BINHEAP
  timer->waiter = NULL;
#else
  timer->waiter = ptr;
#endif
}

static int timer_value(struct fev_timer *timer) { return (int)timer->abs_time.tv_sec; }

static int cmp_timers(const struct fev_timer *lhs, const struct fev_timer *rhs)
{
  long cmp;

  cmp = fev_timespec_cmp(&lhs->abs_time, &rhs->abs_time);
  if (cmp != 0)
    return cmp < 0 ? -1 : 1;

  /* The lhs & rhs pointers are unrelated. */
  if ((uintptr_t)lhs->waiter == (uintptr_t)rhs->waiter)
    return 0;
  if ((uintptr_t)lhs->waiter < (uintptr_t)rhs->waiter)
    return -1;
  return 1;
}

static void check_binheap_indices(struct fev_timers_bucket *bucket)
{
  (void)bucket;

#ifdef FEV_TIMERS_BINHEAP
  struct fev_timer *timer;
  size_t i;

  for (i = 0; i < bucket->len; i++) {
    timer = bucket->heap[i];
    CHECK(timer->index == i, "Wrong index");
  }
#endif
}

static void check_binheap_order(struct fev_timers_bucket *bucket)
{
  (void)bucket;

#ifdef FEV_TIMERS_BINHEAP
  struct fev_timer **heap = bucket->heap;
  size_t len = bucket->len, parent = 0, child = 1;

  while (child < len) {
    CHECK(timer_value(heap[parent]) <= timer_value(heap[child]), "Heap is invalid");
    if (child + 1 == len)
      return;
    CHECK(timer_value(heap[parent]) <= timer_value(heap[child + 1]), "Heap is invalid");
    parent++;
    child = 2 * parent + 1;
  }
#endif
}

static void check_binheap(struct fev_timers_bucket *bucket)
{
  check_binheap_indices(bucket);
  check_binheap_order(bucket);
}

static int add_timer(struct fev_timers_bucket *bucket, struct fev_timer *timer)
{
  int min_changed;

  min_changed = fev_timers_bucket_add(bucket, timer);
  check_binheap(bucket);
  return min_changed;
}

static int del_timer(struct fev_timers_bucket *bucket, struct fev_timer *timer)
{
  int min_changed;

  min_changed = fev_timers_bucket_del(bucket, timer);
  check_binheap(bucket);
  return min_changed;
}

static void del_min_timer(struct fev_timers_bucket *bucket)
{
  struct fev_timer *min;

  min = fev_timers_bucket_min(bucket);
  fev_timers_bucket_del_min(bucket);
  fev_timer_set_expired(min);
  check_binheap(bucket);
}

static void shuffle_timers(struct fev_timer *timers, int len, uint32_t *r)
{
  struct fev_timer tmp;
  uint32_t q = *r;
  int i, j;

  for (i = len - 1; i >= 1; i--) {
    q = FEV_RANDOM_NEXT(q);
    j = (int)(q % (uint32_t)(i + 1));
    tmp = timers[i];
    timers[i] = timers[j];
    timers[j] = tmp;
  }

  *r = q;
}

/* Add and del tests */

/* Simple test */

static void test_add_del_simple(void)
{
  struct fev_timers_bucket bucket;
  struct fev_timer a;
  int min_changed;

  fev_timers_bucket_init(&bucket);
  init_timer(&a, 1, NULL);

  min_changed = add_timer(&bucket, &a);
  CHECK(!(min_changed < 0), "Adding timer failed");
  CHECK(min_changed != 0, "Min should change after adding");

  min_changed = del_timer(&bucket, &a);
  CHECK(min_changed != 0, "Min should change after deleting");

  CHECK(fev_timers_bucket_empty(&bucket), "Bucket should be empty");
  fev_timers_bucket_fini(&bucket);
}

/*
 * Test with two different elements
 * ab_ba means add a, b; then remove b, a.
 */

static void test_add_del_diff_ab_ab(void)
{
  struct fev_timers_bucket bucket;
  struct fev_timer a, b;
  int min_changed;

  fev_timers_bucket_init(&bucket);
  init_timer(&a, 1, NULL);
  init_timer(&b, 2, NULL);

  min_changed = add_timer(&bucket, &a);
  CHECK(!(min_changed < 0), "Adding timer failed");
  CHECK(min_changed != 0, "Min should change after adding a");

  min_changed = add_timer(&bucket, &b);
  CHECK(!(min_changed < 0), "Adding timer failed");
  CHECK(min_changed == 0, "Min should not change after adding b");

  min_changed = del_timer(&bucket, &a);
  CHECK(min_changed != 0, "Min should change after deleting a");

  min_changed = del_timer(&bucket, &b);
  CHECK(min_changed != 0, "Min should change after deleting b");

  CHECK(fev_timers_bucket_empty(&bucket), "Bucket should be empty");
  fev_timers_bucket_fini(&bucket);
}

static void test_add_del_diff_ab_ba(void)
{
  struct fev_timers_bucket bucket;
  struct fev_timer a, b;
  int min_changed;

  fev_timers_bucket_init(&bucket);
  init_timer(&a, 1, NULL);
  init_timer(&b, 2, NULL);

  min_changed = add_timer(&bucket, &a);
  CHECK(!(min_changed < 0), "Adding timer failed");
  CHECK(min_changed != 0, "Min should change after adding a");

  min_changed = add_timer(&bucket, &b);
  CHECK(!(min_changed < 0), "Adding timer failed");
  CHECK(min_changed == 0, "Min should not change after adding b");

  min_changed = del_timer(&bucket, &b);
  CHECK(min_changed == 0, "Min should not change after deleting b");

  min_changed = del_timer(&bucket, &a);
  CHECK(min_changed != 0, "Min should change after deleting a");

  CHECK(fev_timers_bucket_empty(&bucket), "Bucket should be empty");
  fev_timers_bucket_fini(&bucket);
}

static void test_add_del_diff_ba_ab(void)
{
  struct fev_timers_bucket bucket;
  struct fev_timer a, b;
  int min_changed;

  fev_timers_bucket_init(&bucket);
  init_timer(&a, 1, NULL);
  init_timer(&b, 2, NULL);

  min_changed = add_timer(&bucket, &b);
  CHECK(!(min_changed < 0), "Adding timer failed");
  CHECK(min_changed != 0, "Min should change after adding b");

  min_changed = add_timer(&bucket, &a);
  CHECK(!(min_changed < 0), "Adding timer failed");
  CHECK(min_changed != 0, "Min should change after adding a");

  min_changed = del_timer(&bucket, &a);
  CHECK(min_changed != 0, "Min should change after deleting a");

  min_changed = del_timer(&bucket, &b);
  CHECK(min_changed != 0, "Min should change after deleting b");

  CHECK(fev_timers_bucket_empty(&bucket), "Bucket should be empty");
  fev_timers_bucket_fini(&bucket);
}

static void test_add_del_diff_ba_ba(void)
{
  struct fev_timers_bucket bucket;
  struct fev_timer a, b;
  int min_changed;

  fev_timers_bucket_init(&bucket);
  init_timer(&a, 1, NULL);
  init_timer(&b, 2, NULL);

  min_changed = add_timer(&bucket, &b);
  CHECK(!(min_changed < 0), "Adding timer failed");
  CHECK(min_changed != 0, "Min should change after adding b");

  min_changed = add_timer(&bucket, &a);
  CHECK(!(min_changed < 0), "Adding timer failed");
  CHECK(min_changed != 0, "Min should change after adding a");

  min_changed = del_timer(&bucket, &b);
  CHECK(min_changed == 0, "Min should not change after deleting b");

  min_changed = del_timer(&bucket, &a);
  CHECK(min_changed != 0, "Min should change after deleting a");

  CHECK(fev_timers_bucket_empty(&bucket), "Bucket should be empty");
  fev_timers_bucket_fini(&bucket);
}

/* Test with two the same elements */

static void test_add_del_same_ab_ab(void)
{
  struct fev_timers_bucket bucket;
  struct fev_timer a, b;
  int min_changed;

  fev_timers_bucket_init(&bucket);
  init_timer(&a, 1, (void *)1);
  init_timer(&b, 1, (void *)2);

  min_changed = add_timer(&bucket, &a);
  CHECK(!(min_changed < 0), "Adding timer failed");
  CHECK(min_changed != 0, "Min should change after adding a");

  min_changed = add_timer(&bucket, &b);
  CHECK(!(min_changed < 0), "Adding timer failed");
  CHECK(min_changed == 0, "Min should not change after adding b");

  min_changed = del_timer(&bucket, &a);
  CHECK(min_changed != 0, "Min should change after deleting a");

  min_changed = del_timer(&bucket, &b);
  CHECK(min_changed != 0, "Min should change after deleting b");

  CHECK(fev_timers_bucket_empty(&bucket), "Bucket should be empty");
  fev_timers_bucket_fini(&bucket);
}

static void test_add_del_same_ab_ba(void)
{
  struct fev_timers_bucket bucket;
  struct fev_timer a, b;
  int min_changed;

  fev_timers_bucket_init(&bucket);
  init_timer(&a, 1, (void *)1);
  init_timer(&b, 1, (void *)2);

  min_changed = add_timer(&bucket, &a);
  CHECK(!(min_changed < 0), "Adding timer failed");
  CHECK(min_changed != 0, "Min should change after adding a");

  min_changed = add_timer(&bucket, &b);
  CHECK(!(min_changed < 0), "Adding timer failed");
  CHECK(min_changed == 0, "Min should not change after adding b");

  min_changed = del_timer(&bucket, &b);
  CHECK(min_changed == 0, "Min should not change after deleting b");

  min_changed = del_timer(&bucket, &a);
  CHECK(min_changed != 0, "Min should change after deleting a");

  CHECK(fev_timers_bucket_empty(&bucket), "Bucket should be empty");
  fev_timers_bucket_fini(&bucket);
}

/*
 * Tests with more different elements
 * asc_desc means adding in ascending order, then deleting in descending order.
 */

static void test_add_del_diff_asc_asc(void)
{
  struct fev_timers_bucket bucket;
  struct fev_timer timers[NUM_TIMERS];
  int min_changed, i;

  fev_timers_bucket_init(&bucket);

  for (i = 0; i < NUM_TIMERS; i++)
    init_timer(&timers[i], i, NULL);

  for (i = 0; i < NUM_TIMERS; i++) {
    min_changed = add_timer(&bucket, &timers[i]);
    CHECK(!(min_changed < 0), "Adding timer failed");
    CHECK((i == 0) == (min_changed != 0), "Min should change after adding iff the timer is first");
  }

  for (i = 0; i < NUM_TIMERS; i++) {
    min_changed = del_timer(&bucket, &timers[i]);
    CHECK(min_changed != 0, "Min should change after deleting each timer");
  }

  CHECK(fev_timers_bucket_empty(&bucket), "Bucket should be empty");
  fev_timers_bucket_fini(&bucket);
}

static void test_add_del_diff_asc_desc(void)
{
  struct fev_timers_bucket bucket;
  struct fev_timer timers[NUM_TIMERS];
  int min_changed, i;

  fev_timers_bucket_init(&bucket);

  for (i = 0; i < NUM_TIMERS; i++)
    init_timer(&timers[i], i, NULL);

  for (i = 0; i < NUM_TIMERS; i++) {
    min_changed = add_timer(&bucket, &timers[i]);
    CHECK(!(min_changed < 0), "Adding timer failed");
    CHECK((i == 0) == (min_changed != 0), "Min should change after adding iff the timer is first");
  }

  for (i = NUM_TIMERS - 1; i >= 0; i--) {
    min_changed = del_timer(&bucket, &timers[i]);
    CHECK((i == 0) == (min_changed != 0),
          "Min should change after deleting iff the timer is first");
  }

  CHECK(fev_timers_bucket_empty(&bucket), "Bucket should be empty");
  fev_timers_bucket_fini(&bucket);
}

static void test_add_del_diff_desc_asc(void)
{
  struct fev_timers_bucket bucket;
  struct fev_timer timers[NUM_TIMERS];
  int min_changed, i;

  fev_timers_bucket_init(&bucket);

  for (i = 0; i < NUM_TIMERS; i++)
    init_timer(&timers[i], i, NULL);

  for (i = NUM_TIMERS - 1; i >= 0; i--) {
    min_changed = add_timer(&bucket, &timers[i]);
    CHECK(!(min_changed < 0), "Adding timer failed");
    CHECK(min_changed != 0, "Min should change after adding each timer");
  }

  for (i = 0; i < NUM_TIMERS; i++) {
    min_changed = del_timer(&bucket, &timers[i]);
    CHECK(min_changed != 0, "Min should change after deleting each timer");
  }

  CHECK(fev_timers_bucket_empty(&bucket), "Bucket should be empty");
  fev_timers_bucket_fini(&bucket);
}

static void test_add_del_diff_desc_desc(void)
{
  struct fev_timers_bucket bucket;
  struct fev_timer timers[NUM_TIMERS];
  int min_changed, i;

  fev_timers_bucket_init(&bucket);

  for (i = 0; i < NUM_TIMERS; i++)
    init_timer(&timers[i], i, NULL);

  for (i = NUM_TIMERS - 1; i >= 0; i--) {
    min_changed = add_timer(&bucket, &timers[i]);
    CHECK(!(min_changed < 0), "Adding timer failed");
    CHECK(min_changed != 0, "Min should change after adding each timer");
  }

  for (i = NUM_TIMERS - 1; i >= 0; i--) {
    min_changed = del_timer(&bucket, &timers[i]);
    CHECK((i == 0) == (min_changed != 0),
          "Min should change after deleting iff the timer is first");
  }

  CHECK(fev_timers_bucket_empty(&bucket), "Bucket should be empty");
  fev_timers_bucket_fini(&bucket);
}

/* Tests with more the same elements */

static void test_add_del_same_asc_asc(void)
{
  struct fev_timers_bucket bucket;
  struct fev_timer timers[NUM_TIMERS];
  int min_changed, i;

  fev_timers_bucket_init(&bucket);

  for (i = 0; i < NUM_TIMERS; i++)
    init_timer(&timers[i], 1, (void *)(uintptr_t)i);

  for (i = 0; i < NUM_TIMERS; i++) {
    min_changed = add_timer(&bucket, &timers[i]);
    CHECK(!(min_changed < 0), "Adding timer failed");
    CHECK((i == 0) == (min_changed != 0), "Min should change after adding iff the timer is first");
  }

  for (i = 0; i < NUM_TIMERS; i++) {
    min_changed = del_timer(&bucket, &timers[i]);

    /*
     * After deleting the first element, we cannot assume which element is
     * chosen as the min.
     */
    CHECK(i > 0 || min_changed != 0, "Min should change after deleting each timer");
  }

  CHECK(fev_timers_bucket_empty(&bucket), "Bucket should be empty");
  fev_timers_bucket_fini(&bucket);
}

static void test_add_del_same_asc_desc(void)
{
  struct fev_timers_bucket bucket;
  struct fev_timer timers[NUM_TIMERS];
  int min_changed, i;

  fev_timers_bucket_init(&bucket);

  for (i = 0; i < NUM_TIMERS; i++)
    init_timer(&timers[i], 1, (void *)(uintptr_t)i);

  for (i = 0; i < NUM_TIMERS; i++) {
    min_changed = add_timer(&bucket, &timers[i]);
    CHECK(!(min_changed < 0), "Adding timer failed");
    CHECK((i == 0) == (min_changed != 0), "Min should change after adding iff the timer is first");
  }

  for (i = NUM_TIMERS - 1; i >= 0; i--) {
    min_changed = del_timer(&bucket, &timers[i]);
    CHECK((i == 0) == (min_changed != 0),
          "Min should change after deleting iff the timer is first");
  }

  CHECK(fev_timers_bucket_empty(&bucket), "Bucket should be empty");
  fev_timers_bucket_fini(&bucket);
}

/* A test that triggers the change of the min in alternating manner. */

static void test_add_del_alternating(void)
{
  struct fev_timers_bucket bucket;
  struct fev_timer timers[2 * NUM_TIMERS], *timer;
  int min_changed, i;

  fev_timers_bucket_init(&bucket);

  for (i = 0; i < 2 * NUM_TIMERS; i++)
    init_timer(&timers[i], i, NULL);

  for (i = 0; i < NUM_TIMERS; i++) {
    timer = &timers[NUM_TIMERS - i - 1];
    min_changed = add_timer(&bucket, timer);
    CHECK(!(min_changed < 0), "Adding timer failed");
    CHECK(min_changed != 0, "Min should change after adding a timer from the left half");

    timer = &timers[NUM_TIMERS + i];
    min_changed = add_timer(&bucket, timer);
    CHECK(!(min_changed < 0), "Adding timer failed");
    CHECK(min_changed == 0, "Min should not change after adding a timer from the right half");
  }

  for (i = 0; i < NUM_TIMERS; i++) {
    timer = &timers[NUM_TIMERS + i];
    min_changed = del_timer(&bucket, timer);
    CHECK(min_changed == 0, "Min should not change after deleting a timer from the right half");

    timer = &timers[i];
    min_changed = del_timer(&bucket, timer);
    CHECK(min_changed != 0, "Min should change after deleting a timer from the left half");
  }

  CHECK(fev_timers_bucket_empty(&bucket), "Bucket should be empty");
  fev_timers_bucket_fini(&bucket);
}

/* Random tests */

static void test_add_del_diff_random_single(int n, uint32_t *r)
{
  struct fev_timers_bucket bucket;
  struct fev_timer *timers, *timer;
  int i, j, min_changed, value, min_value = INT_MAX;

  fev_timers_bucket_init(&bucket);

  timers = fev_malloc((size_t)n * sizeof(*timers));
  CHECK(timers != NULL, "Allocating timers failed");

  for (i = 0; i < n; i++)
    init_timer(&timers[i], i, NULL);

  shuffle_timers(timers, n, r);

  for (i = 0; i < n; i++) {
    timer = &timers[i];
    value = timer_value(timer);
    min_changed = add_timer(&bucket, timer);
    CHECK(!(min_changed < 0), "Adding timer failed");
    CHECK((value < min_value) == (min_changed != 0),
          "Min should change after adding smaller element");
    if (value < min_value)
      min_value = value;
  }

  min_value = 0;
  for (i = 0; i < n; i++) {
    timer = &timers[i];
    value = timer_value(timer);
    min_changed = del_timer(&bucket, timer);
    CHECK((value == min_value) == (min_changed != 0),
          "Min should change after deleting the smallest element");
    timer->abs_time.tv_sec = INT_MAX;

    if (value == min_value) {
      /* Find the next min value. */
      min_value = INT_MAX;
      for (j = 0; j < n; j++) {
        value = timer_value(&timers[j]);
        if (value < min_value)
          min_value = value;
      }
    }
  }

  fev_free(timers);

  CHECK(fev_timers_bucket_empty(&bucket), "Bucket should be empty");
  fev_timers_bucket_fini(&bucket);
}

static void test_add_del_diff_random(void)
{
  uint32_t r = SEED;
  int i, n;

  for (i = 0; i < NUM_RANDOM_TRIES; i++) {
    r = FEV_RANDOM_NEXT(r);
    n = ((int)r % NUM_TIMERS) + 1;
    r = FEV_RANDOM_NEXT(r);
    test_add_del_diff_random_single(n, &r);
  }
}

static void test_add_del_random_single(int n, uint32_t *r)
{
  struct fev_timers_bucket bucket;
  struct fev_timer *timers, *timer, *min = NULL, *next_min;
  int i, min_changed, value;
  bool should_change;

  fev_timers_bucket_init(&bucket);

  timers = fev_malloc((size_t)n * sizeof(*timers));
  CHECK(timers != NULL, "Allocating timers failed");

  for (i = 0; i < n; i++) {
    *r = FEV_RANDOM_NEXT(*r);
    value = (int)(*r % (uint32_t)(n / 2 + 1));
    init_timer(&timers[i], value, (void *)(uintptr_t)i);
  }

  for (i = 0; i < n; i++) {
    timer = &timers[i];

    should_change = min == NULL || cmp_timers(timer, min) < 0;

    min_changed = add_timer(&bucket, timer);
    CHECK(!(min_changed < 0), "Adding timer failed");
    CHECK(should_change == (min_changed != 0), "Min should change after adding smaller element");

    next_min = fev_timers_bucket_min(&bucket);
    CHECK(should_change == (next_min != min), "Min should change");
    min = next_min;
  }

  for (i = 0; i < n; i++) {
    timer = &timers[i];

    should_change = timer == min;

    min_changed = del_timer(&bucket, timer);
    CHECK(should_change == (min_changed != 0),
          "Min should change after deleting the smallest element");

    if (i < n - 1) {
      next_min = fev_timers_bucket_min(&bucket);
      CHECK(should_change == (next_min != min), "Min should change");
      min = next_min;
    }
  }

  fev_free(timers);

  CHECK(fev_timers_bucket_empty(&bucket), "Bucket should be empty");
  fev_timers_bucket_fini(&bucket);
}

static void test_add_del_random(void)
{
  uint32_t r = SEED;
  int i, n;

  for (i = 0; i < NUM_RANDOM_TRIES; i++) {
    r = FEV_RANDOM_NEXT(r);
    n = ((int)r % NUM_TIMERS) + 1;
    r = FEV_RANDOM_NEXT(r);
    test_add_del_random_single(n, &r);
  }
}

/* Add and del min tests */

/* Random tests */

#define K 3

static void test_add_del_min_random_one(int n, uint32_t *r)
{
  struct fev_timers_bucket bucket;
  struct fev_timer *timers, *timer;
  int i, j, min_changed, value, min_value = INT_MAX;

  fev_timers_bucket_init(&bucket);

  timers = fev_malloc(3 * (size_t)n * sizeof(*timers));
  CHECK(timers != NULL, "Allocating timers failed");

  for (i = 0; i < n; i++) {
    for (j = 0; j < K; j++) {
      init_timer(&timers[i * K + j], i, (void *)(uintptr_t)j);
    }
  }

  shuffle_timers(timers, 3 * n, r);

  for (i = 0; i < n; i++) {
    for (j = 0; j < K; j++) {
      timer = &timers[i * K + j];
      value = timer_value(timer);
      min_changed = add_timer(&bucket, timer);
      CHECK(!(min_changed < 0), "Adding timer failed");
      CHECK(value >= min_value || (min_changed != 0),
            "Min should change after adding smaller element");
      if (value < min_value)
        min_value = value;
    }
  }

  for (i = 0; i < n; i++) {
    for (j = 0; j < K; j++) {
      timer = fev_timers_bucket_min(&bucket);
      value = timer_value(timer);
      CHECK(value == i, "Wrong value of the min timer");

      del_min_timer(&bucket);
      CHECK(fev_timer_is_expired(timer), "Timer should be deleted");
    }
  }

  fev_free(timers);

  CHECK(fev_timers_bucket_empty(&bucket), "Bucket should be empty");
  fev_timers_bucket_fini(&bucket);
}

static void test_add_del_min_random(void)
{
  uint32_t r = SEED;
  int i, n;

  for (i = 0; i < NUM_RANDOM_TRIES; i++) {
    r = FEV_RANDOM_NEXT(r);
    n = ((int)r % NUM_TIMERS) + 1;
    r = FEV_RANDOM_NEXT(r);
    test_add_del_min_random_one(n, &r);
  }
}

int main(void)
{
  test_add_del_simple();

  test_add_del_diff_ab_ab();
  test_add_del_diff_ab_ba();
  test_add_del_diff_ba_ab();
  test_add_del_diff_ba_ba();

  test_add_del_same_ab_ab();
  test_add_del_same_ab_ba();

  test_add_del_diff_asc_asc();
  test_add_del_diff_asc_desc();
  test_add_del_diff_desc_asc();
  test_add_del_diff_desc_desc();

  test_add_del_same_asc_asc();
  test_add_del_same_asc_desc();

  test_add_del_alternating();

  test_add_del_diff_random();
  test_add_del_random();

  test_add_del_min_random();

  return 0;
}
