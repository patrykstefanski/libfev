/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

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

  movl 4(%esp), %eax
  movl 8(%esp), %ecx

  stmxcsr (%eax)
  fnstcw 4(%eax)
  movl %esp, 8(%eax)
  movl %ebp, 12(%eax)
  movl %ebx, 16(%eax)
  movl %edi, 20(%eax)
  movl %esi, 24(%eax)

  ldmxcsr (%ecx)
  fldcw 4(%ecx)
  movl 8(%ecx), %esp
  movl 12(%ecx), %ebp
  movl 16(%ecx), %ebx
  movl 20(%ecx), %edi
  movl 24(%ecx), %esi

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

  movl 12(%esp), %eax
  movl 16(%esp), %ecx

  stmxcsr (%eax)
  fnstcw 4(%eax)
  movl %esp, 8(%eax)
  movl %ebp, 12(%eax)
  movl %ebx, 16(%eax)
  movl %edi, 20(%eax)
  movl %esi, 24(%eax)

  mov 4(%esp), %eax
  mov 8(%esp), %edx

  ldmxcsr (%ecx)
  fldcw 4(%ecx)
  movl 8(%ecx), %esp
  movl 12(%ecx), %ebp
  movl 16(%ecx), %ebx
  movl 20(%ecx), %edi
  movl 24(%ecx), %esi

  pushl %eax
  call *%edx
  popl %eax

  ret

#ifdef __ELF__
.size fev_context_switch_and_call,.-fev_context_switch_and_call
#endif


#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
