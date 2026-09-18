// runtime/boot/x86/grub_mb2.S
// Multiboot2 header + GRUB entry shim for 32-bit x86.

  .section .multiboot2
  .align 8
  .global multiboot2_header
multiboot2_header:
  .long 0xE85250D6        // magic
  .long 0                 // architecture (i386)
  .long (multiboot2_header_end - multiboot2_header)
  .long -(0xE85250D6 + 0 + (multiboot2_header_end - multiboot2_header))

#ifdef TEZZ_GRUB_GFX
  // framebuffer tag (type=5, size=20)
  .short 5
  .short 0
  .long 20
  .long 0
  .long 0
  .long 0
  .align 8
#endif

  // end tag (type=0, flags=0, size=8)
  .short 0
  .short 0
  .long 8
multiboot2_header_end:

  .text
  .code32
  .global grub_entry
  .extern _start
grub_entry:
  cli
  mov %eax, grub_mb2_magic
  movl $0, grub_mb2_magic+4
  mov %ebx, grub_mb2_info
  movl $0, grub_mb2_info+4
  movl $1, grub_booted
  movl $0, grub_booted+4
  mov $0x00E9, %dx
  mov $'G', %al
  outb %al, %dx
  mov $'3', %al
  outb %al, %dx
  mov $'2', %al
  outb %al, %dx
  mov $'\n', %al
  outb %al, %dx
  jmp _start

  .data
  .align 8
  .global grub_mb2_magic
  .global grub_mb2_info
  .global grub_booted
grub_mb2_magic:
  .quad 0
grub_mb2_info:
  .quad 0
grub_booted:
  .quad 0
