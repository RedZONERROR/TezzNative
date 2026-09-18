// runtime/boot/x86_64/paging.S
// Minimal x86_64 paging helpers (System V ABI)

  .text
  .global arch_load_cr3
  .global arch_enable_paging
  .global arch_invlpg

arch_load_cr3:
  mov %rdi, %cr3
  ret

arch_enable_paging:
  // Enable PAE
  mov %cr4, %rax
  orq $0x20, %rax
  mov %rax, %cr4
  // Enable LME in EFER
  mov $0xC0000080, %ecx
  rdmsr
  orl $0x100, %eax
  wrmsr
  // Enable paging
  mov %cr0, %rax
  orl $0x80000000, %eax
  mov %rax, %cr0
  ret

arch_invlpg:
  invlpg (%rdi)
  ret
