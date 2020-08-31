/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#include "fev_stack.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>

#include "fev_assert.h"
#include "fev_compiler.h"
#include "fev_util.h"

#if defined(FEV_OS_BSD) || defined(FEV_OS_MACOS) || defined(FEV_OS_DARWIN)
#define FEV_MAP_ANON MAP_ANON
#elif defined(FEV_OS_LINUX)
#define FEV_MAP_ANON MAP_ANONYMOUS
#else
#error Not implemented
#endif

int fev_stack_alloc(void **addr_ptr, size_t usable_size, size_t guard_size)
{
  size_t total_size;
  void *addr;
  int ret;

  FEV_ASSERT(usable_size <= SIZE_MAX - guard_size);
  total_size = usable_size + guard_size;

  addr = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | FEV_MAP_ANON, -1, 0);
  if (FEV_UNLIKELY(addr == MAP_FAILED)) {
    ret = -errno;
    goto fail;
  }

  if (guard_size > 0) {
    ret = mprotect(addr, guard_size, PROT_NONE);
    if (FEV_UNLIKELY(ret != 0)) {
      ret = -errno;
      goto fail_free;
    }
  }

  *addr_ptr = addr;
  return 0;

fail_free:
  fev_stack_free(addr, total_size);

fail:
  return ret;
}

void fev_stack_free(void *addr, size_t total_stack_size)
{
  int ret = munmap(addr, total_stack_size);
  (void)ret;
  FEV_ASSERT(ret == 0);
}
