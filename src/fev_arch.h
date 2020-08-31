/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_ARCH_H
#define FEV_ARCH_H

#include <fev/fev.h>

#if defined(FEV_ARCH_I386) || defined(FEV_ARCH_X86_64)
#include "fev_arch_x86.h"
#else
#error Your architecture is unsupported, currently only x86 is supported.
#endif

#endif /* !FEV_ARCH_H */
