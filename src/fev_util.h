/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_UTIL_H
#define FEV_UTIL_H

#include <stddef.h>
#include <stdint.h>

#define FEV_CONTAINER_OF(ptr, type, member) ((type *)((uint8_t *)(ptr)-offsetof(type, member)))

/* Lehmer RNG. */
#define FEV_RANDOM_MULTIPLIER UINT64_C(48271)
#define FEV_RANDOM_MODULUS UINT64_C(2147483647)
#define FEV_RANDOM_MAX (FEV_RANDOM_MODULUS - 1)
#define FEV_RANDOM_NEXT(r) ((uint32_t)((uint64_t)(r)*FEV_RANDOM_MULTIPLIER % FEV_RANDOM_MODULUS))

#endif /* !FEV_UTIL_H */
