/* runtime/boot/x86/entry.S */

  .section .text.boot, "ax"
  .global _start
  .extern tn_kernel_main
  .extern __bss_start
  .extern __bss_end

_start:
  cli
  movl $__bss_start, %edi
  movl $__bss_end, %ecx
1:
  cmpl %ecx, %edi
  jae 2f
  movb $0, (%edi)
  incl %edi
  jmp 1b
2:
  movl $stack_top, %esp
  call tn_kernel_main
3:
  hlt
  jmp 3b

  .section .bss.stack, "aw", @nobits
  .align 16
stack:
  .skip 32768
stack_top:
