/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_TIME_H
#define FEV_TIME_H

#include <fev/fev.h>

#include <stdint.h>
#include <time.h>

#include "fev_assert.h"
#include "fev_compiler.h"

/* TODO: Move it to config. */
#ifndef FEV_CLOCK_ID
#define FEV_CLOCK_ID CLOCK_MONOTONIC
#endif

#define FEV_NSECS_PER_SEC (1000 * 1000 * 1000)

FEV_NONNULL(1) static inline void fev_clock_get_time(struct timespec *ts)
{
  int ret;

  ret = clock_gettime(FEV_CLOCK_ID, ts);
  (void)ret;
  FEV_ASSERT(ret == 0);
}

FEV_NONNULL(1) static inline void fev_timespec_assert_valid(const struct timespec *ts)
{
  (void)ts;
  FEV_ASSERT(ts->tv_sec >= 0);
  FEV_ASSERT(ts->tv_nsec >= 0 && ts->tv_nsec < FEV_NSECS_PER_SEC);
}

FEV_NONNULL(1, 2)
FEV_PURE static inline long fev_timespec_cmp(const struct timespec *lhs, const struct timespec *rhs)
{
  time_t lhs_sec, rhs_sec;
  long sec_cmp;

  fev_timespec_assert_valid(lhs);
  fev_timespec_assert_valid(rhs);

  /* tv_sec is of type time_t, which is unspecified (it may be unsigned). */
  lhs_sec = lhs->tv_sec;
  rhs_sec = rhs->tv_sec;
  sec_cmp = _Generic(lhs_sec, long
                     : lhs_sec - rhs_sec, default
                     : lhs_sec < rhs_sec ? -1 : (lhs_sec > rhs_sec ? 1 : 0));
  if (sec_cmp != 0)
    return sec_cmp;

  return lhs->tv_nsec - rhs->tv_nsec;
}

FEV_NONNULL(1, 2)
static inline void fev_timespec_abs_to_rel(struct timespec *rel_time,
                                           const struct timespec *abs_time)
{
  struct timespec ts;

  fev_timespec_assert_valid(abs_time);

  fev_clock_get_time(&ts);

  if (fev_timespec_cmp(&ts, abs_time) >= 0) {
    rel_time->tv_sec = 0;
    rel_time->tv_nsec = 0;
    return;
  }

  ts.tv_sec = abs_time->tv_sec - ts.tv_sec;

  ts.tv_nsec = abs_time->tv_nsec - ts.tv_nsec;
  if (ts.tv_nsec < 0) {
    FEV_ASSERT(ts.tv_sec > 0);
    ts.tv_sec -= 1;
    ts.tv_nsec += FEV_NSECS_PER_SEC;
  }

  fev_timespec_assert_valid(&ts);

  rel_time->tv_sec = ts.tv_sec;
  rel_time->tv_nsec = ts.tv_nsec;
}

FEV_NONNULL(1, 2)
static inline void fev_get_abs_time_since_now(struct timespec *restrict abs_time,
                                              const struct timespec *restrict rel_time)
{
  fev_timespec_assert_valid(rel_time);

  fev_clock_get_time(abs_time);

  abs_time->tv_sec +=
      rel_time->tv_sec + (abs_time->tv_nsec + rel_time->tv_nsec) / FEV_NSECS_PER_SEC;
  abs_time->tv_nsec = (abs_time->tv_nsec + rel_time->tv_nsec) % FEV_NSECS_PER_SEC;

  fev_timespec_assert_valid(abs_time);
}

FEV_NONNULL(1) static inline uint64_t fev_timespec_to_ns(const struct timespec *ts)
{
  uint64_t sec, nsec, x, ret;

  fev_timespec_assert_valid(ts);

  sec = (uint64_t)ts->tv_sec;
  nsec = (uint64_t)ts->tv_nsec;

  if (sec > UINT64_MAX / FEV_NSECS_PER_SEC)
    x = UINT64_MAX;
  else
    x = sec * FEV_NSECS_PER_SEC;

  ret = x + nsec;
  if (ret < x)
    ret = UINT64_MAX;

  return ret;
}

#endif /* !FEV_TIME_H */
