// runtime/boot/x86_64/tsc.S
// Read TSC (returns 64-bit).

  .text
  .global arch_rdtsc

arch_rdtsc:
  rdtsc
  shl $32, %rdx
  orq %rdx, %rax
  ret
