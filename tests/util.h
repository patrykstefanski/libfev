#ifndef FEV_TESTS_UTIL_H
#define FEV_TESTS_UTIL_H

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define FATAL(fmt, ...)                                                                            \
  do {                                                                                             \
    fprintf(stderr, fmt "\n", ##__VA_ARGS__);                                                      \
    abort();                                                                                       \
  } while (0)

#define CHECK(expr, fmt, ...)                                                                      \
  do {                                                                                             \
    if (!(expr))                                                                                   \
      FATAL("Asseration \"%s\" failed in %s (%s:%u): " fmt, #expr, __func__, __FILE__, __LINE__,   \
            ##__VA_ARGS__);                                                                        \
  } while (0)

#define DEF_PARSE(type, scan_fmt, print_fmt)                                                       \
  static inline type parse_##type(const char *str, const char *name, type *min_val)                \
  {                                                                                                \
    type val;                                                                                      \
    int num_scanned = sscanf(str, "%" scan_fmt, &val);                                             \
    CHECK(num_scanned == 1, "Failed to parse '%s' as %s", str, name);                              \
    CHECK(min_val == NULL || val >= *min_val, "%s must be at least %" print_fmt, name, *min_val);  \
    return val;                                                                                    \
  }

DEF_PARSE(uint32_t, SCNu32, PRIu32)
DEF_PARSE(uint64_t, SCNu64, PRIu64)

#undef DEF_PARSE

#endif /* !FEV_TESTS_UTIL_H */
