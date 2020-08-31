/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_ALLOC_H
#define FEV_ALLOC_H

#include <fev/fev.h>

#include <stddef.h>
#include <stdint.h>

#include "fev_assert.h"
#include "fev_compiler.h"

#ifdef FEV_ASSUME_MALLOC_NEVER_FAILS
#define FEV_ALLOC_RETURN FEV_RETURNS_NONNULL
#else
#define FEV_ALLOC_RETURN
#endif

extern fev_realloc_t fev_realloc_ptr;

FEV_ALLOC_RETURN FEV_ALLOC_SIZE(1) FEV_MALLOC static inline void *fev_malloc(size_t size)
{
  void *ptr;

  ptr = fev_realloc_ptr(NULL, size);

  /* The returned pointer should be at least aligned to sizeof(void *). */
  FEV_ASSERT((uintptr_t)ptr % sizeof(void *) == 0);

  return ptr;
}

FEV_ALLOC_RETURN FEV_ALLOC_SIZE(2) static inline void *fev_realloc(void *ptr, size_t size)
{
  return fev_realloc_ptr(ptr, size);
}

static inline void fev_free(void *ptr)
{
  if (ptr != NULL)
    fev_realloc_ptr(ptr, 0);
}

FEV_ALLOC_RETURN FEV_ALLOC_ALIGN(1) FEV_ALLOC_SIZE(2) FEV_MALLOC
    static inline void *fev_aligned_alloc(size_t alignment, size_t size)
{
  void *ptr, *ret;

  FEV_ASSERT(alignment != 0);
  FEV_ASSERT(alignment % sizeof(void *) == 0);
  FEV_ASSERT((alignment & (alignment - 1)) == 0);
  FEV_ASSERT(size <= SIZE_MAX - alignment);

  ptr = fev_malloc(size + alignment);

#ifndef FEV_ASSUME_MALLOC_NEVER_FAILS
  if (FEV_UNLIKELY(ptr == NULL))
    return NULL;
#endif

  ret = (void *)(((uintptr_t)ptr + alignment) & ~(alignment - 1));

  /*
   * Check if [ret, ret+size) is valid (or the difference below is no more than
   * `alignment` since we allocated `size` + `alignment` bytes.
   */
  FEV_ASSERT((uintptr_t)ret - (uintptr_t)ptr <= alignment);

  FEV_ASSERT((uintptr_t)ptr <= (uintptr_t)(&((void **)ret)[-1]));
  ((void **)ret)[-1] = ptr;

  return ret;
}

FEV_NONNULL(1) static inline void fev_aligned_free(void *ptr) { fev_free(((void **)ptr)[-1]); }

#endif /* !FEV_ALLOC_H */
