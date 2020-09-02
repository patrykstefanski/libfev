/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#include <fev/config.h>

.text


.p2align 4,,15

#ifdef __APPLE__
.globl _fev_context_switch
#else
.globl fev_context_switch
#endif

#ifdef __ELF__
.type fev_context_switch,@function
#endif

#ifdef __APPLE__
_fev_context_switch:
#else
fev_context_switch:
#endif

#ifdef FEV_ENABLE_ASAN
  pushq %rdi
  pushq %rsi
  leaq 64(%rdi), %rdi /* &from->fake_stack */
  movq 80(%rsi), %rdx /* to->stack_size */
  movq 72(%rsi), %rsi /* to->stack_bottom */
  call __sanitizer_start_switch_fiber
  popq %rsi
  popq %rdi
#endif

  stmxcsr (%rdi)
  fnstcw 4(%rdi)
  movq %rsp, 8(%rdi)
  movq %rbp, 16(%rdi)
  movq %rbx, 24(%rdi)
  movq %r12, 32(%rdi)
  movq %r13, 40(%rdi)
  movq %r14, 48(%rdi)
  movq %r15, 56(%rdi)

  ldmxcsr (%rsi)
  fldcw 4(%rsi)
  movq 8(%rsi), %rsp
  movq 16(%rsi), %rbp
  movq 24(%rsi), %rbx
  movq 32(%rsi), %r12
  movq 40(%rsi), %r13
  movq 48(%rsi), %r14
  movq 56(%rsi), %r15

#ifdef FEV_ENABLE_ASAN
  movq %rdi, %rax
  movq 64(%rsi), %rdi /* to->fake_stack */
  leaq 72(%rax), %rsi /* &from->stack_bottom */
  leaq 80(%rax), %rdx /* &from->stack_size */
  call __sanitizer_finish_switch_fiber
#endif

  ret

#ifdef __ELF__
.size fev_context_switch,.-fev_context_switch
#endif


.p2align 4,,15

#ifdef __APPLE__
.globl _fev_context_switch_and_call
#else
.globl fev_context_switch_and_call
#endif

#ifdef __ELF__
.type fev_context_switch_and_call,@function
#endif

#ifdef __APPLE__
_fev_context_switch_and_call:
#else
fev_context_switch_and_call:
#endif

#ifdef FEV_ENABLE_ASAN
  pushq %rdi
  pushq %rsi
  pushq %rdx
  pushq %rcx
  leaq 64(%rdx), %rdi /* &from->fake_stack */
  movq 72(%rcx), %rsi /* to->stack_bottom */
  movq 80(%rcx), %rdx /* to->stack_size */
  call __sanitizer_start_switch_fiber
  popq %rcx
  popq %rdx
  popq %rsi
  popq %rdi
#endif

  stmxcsr (%rdx)
  fnstcw 4(%rdx)
  movq %rsp, 8(%rdx)
  movq %rbp, 16(%rdx)
  movq %rbx, 24(%rdx)
  movq %r12, 32(%rdx)
  movq %r13, 40(%rdx)
  movq %r14, 48(%rdx)
  movq %r15, 56(%rdx)

  ldmxcsr (%rcx)
  fldcw 4(%rcx)
  movq 8(%rcx), %rsp
  movq 16(%rcx), %rbp
  movq 24(%rcx), %rbx
  movq 32(%rcx), %r12
  movq 40(%rcx), %r13
  movq 48(%rcx), %r14
  movq 56(%rcx), %r15

#ifdef FEV_ENABLE_ASAN
  pushq %rdi
  pushq %rsi
  movq 64(%rcx), %rdi /* to->fake_stack */
  leaq 72(%rdx), %rsi /* &from->stack_bottom */
  leaq 80(%rdx), %rdx /* &from->stack_size */
  call __sanitizer_finish_switch_fiber
  popq %rsi
  popq %rdi
#endif

  jmp *%rsi

#ifdef __ELF__
.size fev_context_switch_and_call,.-fev_context_switch_and_call
#endif


#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
