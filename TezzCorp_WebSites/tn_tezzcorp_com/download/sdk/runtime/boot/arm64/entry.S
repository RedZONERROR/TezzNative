/* runtime/boot/arm64/entry.S */

  .section .text.boot, "ax"
  .global _start
  .extern tn_kernel_main
  .extern __bss_start
  .extern __bss_end

_start:
  ldr x0, =__bss_start
  ldr x1, =__bss_end
1:
  cmp x0, x1
  b.hs 2f
  str xzr, [x0], #8
  b 1b
2:
  ldr x2, =stack_top
  mov sp, x2
  bl tn_kernel_main
3:
  wfi
  b 3b

  .section .bss.stack, "aw", %nobits
  .align 16
stack:
  .skip 65536
stack_top:
