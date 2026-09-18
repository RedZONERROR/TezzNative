/* runtime/boot/riscv64/entry.S */

  .section .text.boot, "ax"
  .globl _start
  .extern tn_kernel_main
  .extern __bss_start
  .extern __bss_end

_start:
  la t0, __bss_start
  la t1, __bss_end
1:
  bgeu t0, t1, 2f
  sd zero, 0(t0)
  addi t0, t0, 8
  j 1b
2:
  la sp, stack_top
  call tn_kernel_main
3:
  wfi
  j 3b

  .section .bss.stack, "aw", @nobits
  .align 16
stack:
  .space 65536
stack_top:
