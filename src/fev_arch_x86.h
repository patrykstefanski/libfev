/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_ARCH_X86_H
#define FEV_ARCH_X86_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "fev_assert.h"
#include "fev_compiler.h"

/* pause */

static inline void fev_pause(void) { asm volatile("pause"); }

/* cmpxchg8b/cmpxchg16b */

#if defined(FEV_ARCH_X86_64)
#define FEV_CMPXCHG2_OPCODE "cmpxchg16b"
#else
#define FEV_CMPXCHG2_OPCODE "cmpxchg8b"
#endif

#define fev_cmpxchg2(ptr, expected0, expected1, desired0, desired1)                                \
  ({                                                                                               \
    bool fev_exchanged;                                                                            \
    FEV_ASSERT((uintptr_t)(ptr) % (2 * sizeof(void *)) == 0);                                      \
    static_assert(sizeof(*(expected0)) == sizeof(void *), "wrong size");                           \
    static_assert(sizeof(*(expected1)) == sizeof(void *), "wrong size");                           \
    static_assert(sizeof(desired0) == sizeof(void *), "wrong size");                               \
    static_assert(sizeof(desired1) == sizeof(void *), "wrong size");                               \
    asm volatile("lock; " FEV_CMPXCHG2_OPCODE " %1"                                                \
                 : "=@cce"(fev_exchanged), "+m"(*(uintptr_t *)(ptr)), "+a"(*(expected0)),          \
                   "+d"(*(expected1))                                                              \
                 : "b"(desired0), "c"(desired1)                                                    \
                 : "memory", "cc");                                                                \
    fev_exchanged;                                                                                 \
  })

#endif /* !FEV_ARCH_X86_H */
