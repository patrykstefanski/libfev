/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_COMPILER_H
#define FEV_COMPILER_H

#include <fev/fev.h>

/* Attributes */

/* FEV_NONNULL, FEV_NORETURN and FEV_PURE are defined in fev/fev.h. */

#if FEV_HAS_ATTRIBUTE(__always_inline__) || FEV_GCC_VERSION
#define FEV_ALWAYS_INLINE __attribute__((__always_inline__))
#else
#define FEV_ALWAYS_INLINE /* __attribute__((__always_inline__)) */
#endif

#if FEV_HAS_ATTRIBUTE(__aligned__) || FEV_GCC_VERSION
#define FEV_ALIGNED(alignment) __attribute__((__aligned__(alignment)))
#else
#define FEV_ALIGNED(alignment) /* __attribute__((__aligned__(alignment))) */
#endif

#if FEV_HAS_ATTRIBUTE(__const__) || FEV_GCC_VERSION
#define FEV_CONST __attribute__((__const__))
#else
#define FEV_CONST /* __attribute__((__const__)) */
#endif

#if FEV_HAS_ATTRIBUTE(__malloc__) || FEV_GCC_VERSION
#define FEV_MALLOC __attribute__((__malloc__))
#else
#define FEV_MALLOC /* __attribute__((__malloc__)) */
#endif

#if FEV_HAS_ATTRIBUTE(__noinline__) || FEV_GCC_VERSION
#define FEV_NOINLINE __attribute__((__noinline__))
#else
#define FEV_NOINLINE /* __attribute__((__noinline__)) */
#endif

#if FEV_HAS_ATTRIBUTE(__warn_unused_result__) || FEV_GCC_VERSION
#define FEV_WARN_UNUSED_RESULT __attribute__((__warn_unused_result__))
#else
#define FEV_WARN_UNUSED_RESULT /* __attribute__((__warn_unused_result__)) */
#endif

#if FEV_HAS_ATTRIBUTE(__alloc_size__) || FEV_GCC_VERSION >= 40300
#define FEV_ALLOC_SIZE(...) __attribute__((__alloc_size__(__VA_ARGS__)))
#else
#define FEV_ALLOC_SIZE(...) /* __attribute__((__alloc_size__(__VA_ARGS__))) */
#endif

#if FEV_HAS_ATTRIBUTE(__cold__) || FEV_GCC_VERSION >= 40300
#define FEV_COLD __attribute__((__cold__))
#else
#define FEV_COLD /* __attribute__((__cold__)) */
#endif

#if FEV_HAS_ATTRIBUTE(__hot__) || FEV_GCC_VERSION >= 40300
#define FEV_HOT __attribute__((__hot__))
#else
#define FEV_HOT /* __attribute__((__hot__)) */
#endif

#if FEV_HAS_ATTRIBUTE(__alloc_align__) || FEV_GCC_VERSION >= 40900
#define FEV_ALLOC_ALIGN(n) __attribute__((__alloc_align__(n)))
#else
#define FEV_ALLOC_ALIGN(n) /* __attribute__((__alloc_align__(n))) */
#endif

#if FEV_HAS_ATTRIBUTE(__returns_nonnull__) || FEV_GCC_VERSION >= 40900
#define FEV_RETURNS_NONNULL __attribute__((__returns_nonnull__))
#else
#define FEV_RETURNS_NONNULL /* __attribute__((__returns_nonnull__)) */
#endif

/* Builtins */

#ifdef __has_builtin
#define FEV_HAS_BUILTIN(builtin) __has_builtin(builtin)
#else
#define FEV_HAS_BUILTIN(builtin) 0
#endif

#if FEV_HAS_BUILTIN(__builtin_expect) || FEV_GCC_VERSION
#define FEV_LIKELY(exp) __builtin_expect((exp), 1)
#define FEV_UNLIKELY(exp) __builtin_expect((exp), 0)
#else
#define FEV_LIKELY(exp) (exp)
#define FEV_UNLIKELY(exp) (exp)
#endif

#if FEV_HAS_BUILTIN(__builtin_unreachable) || FEV_GCC_VERSION >= 40500
#define FEV_UNREACHABLE() __builtin_unreachable()
#else
#define FEV_UNREACHABLE() /* __builtin_unreachable() */
#endif

#endif /* !FEV_COMPILER_H */
