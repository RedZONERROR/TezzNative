// runtime/boot/x86_64/grub_mb2.S
// Multiboot2 header + GRUB entry shim.

  .section .multiboot2
  .align 8
  .global multiboot2_header
multiboot2_header:
  .long 0xE85250D6        // magic
  .long 0                 // architecture (i386)
  .long (multiboot2_header_end - multiboot2_header) // header length
  .long -(0xE85250D6 + 0 + (multiboot2_header_end - multiboot2_header)) // checksum

#ifdef TEZZ_GRUB_GFX
  // framebuffer tag (type=5, size=20)
  .short 5
  .short 0
  .long 20
  .long 0   // width (0 = any)
  .long 0   // height (0 = any)
  .long 0   // depth (0 = any)
  .align 8
#endif

  // end tag (type=0, flags=0, size=8)
  .short 0
  .short 0
  .long 8
multiboot2_header_end:

  .text
  .global grub_entry
grub_entry:
  // GRUB multiboot2 entry (32-bit). Emit debugcon marker, then enter long mode.
  .code32
  cli
  // Save multiboot2 magic/info for framebuffer parsing in C (before clobbering %eax).
  mov %eax, grub_mb2_magic
  movl $0, grub_mb2_magic+4
  mov %ebx, grub_mb2_info
  movl $0, grub_mb2_info+4
  movl $1, grub_booted
  movl $0, grub_booted+4
  // debugcon helper: write a single char in AL to port 0xE9
  mov $0x00E9, %dx
  mov $0x00E9, %dx
  mov $'G', %al
  outb %al, %dx
  mov $'R', %al
  outb %al, %dx
  mov $'U', %al
  outb %al, %dx
  mov $'B', %al
  outb %al, %dx
  mov $'\n', %al
  outb %al, %dx


  // Set 32-bit stack
  mov $stack32_top, %esp
  mov $'1', %al
  outb %al, %dx
  mov $'\n', %al
  outb %al, %dx

  // Zero page tables
  mov $pml4, %edi
  mov $1024, %ecx
  xor %eax, %eax
  rep stosl
  mov $pdpt, %edi
  mov $1024, %ecx
  xor %eax, %eax
  rep stosl
  mov $pd0, %edi
  mov $1024, %ecx
  xor %eax, %eax
  rep stosl
  mov $pd1, %edi
  mov $1024, %ecx
  xor %eax, %eax
  rep stosl
  mov $pd2, %edi
  mov $1024, %ecx
  xor %eax, %eax
  rep stosl
  mov $pd3, %edi
  mov $1024, %ecx
  xor %eax, %eax
  rep stosl
  mov $pd4, %edi
  mov $1024, %ecx
  xor %eax, %eax
  rep stosl
  mov $pd5, %edi
  mov $1024, %ecx
  xor %eax, %eax
  rep stosl
  mov $pd6, %edi
  mov $1024, %ecx
  xor %eax, %eax
  rep stosl
  mov $pd7, %edi
  mov $1024, %ecx
  xor %eax, %eax
  rep stosl
  mov $pd8, %edi
  mov $1024, %ecx
  xor %eax, %eax
  rep stosl
  mov $pd9, %edi
  mov $1024, %ecx
  xor %eax, %eax
  rep stosl
  mov $pd10, %edi
  mov $1024, %ecx
  xor %eax, %eax
  rep stosl
  mov $pd11, %edi
  mov $1024, %ecx
  xor %eax, %eax
  rep stosl
  mov $pd12, %edi
  mov $1024, %ecx
  xor %eax, %eax
  rep stosl
  mov $pd13, %edi
  mov $1024, %ecx
  xor %eax, %eax
  rep stosl
  mov $pd14, %edi
  mov $1024, %ecx
  xor %eax, %eax
  rep stosl
  mov $pd15, %edi
  mov $1024, %ecx
  xor %eax, %eax
  rep stosl
  mov $'2', %al
  outb %al, %dx
  mov $'\n', %al
  outb %al, %dx

  // pml4[0] = pdpt | 0x03
  mov $pdpt, %eax
  or $0x03, %eax
  mov %eax, pml4
  movl $0, pml4+4

  // pdpt[0..15] = pd0..pd15 | 0x03 (map first 16GiB)
  mov $pd0, %eax
  or $0x03, %eax
  mov %eax, pdpt
  movl $0, pdpt+4
  mov $pd1, %eax
  or $0x03, %eax
  mov %eax, pdpt+8
  movl $0, pdpt+12
  mov $pd2, %eax
  or $0x03, %eax
  mov %eax, pdpt+16
  movl $0, pdpt+20
  mov $pd3, %eax
  or $0x03, %eax
  mov %eax, pdpt+24
  movl $0, pdpt+28
  mov $pd4, %eax
  or $0x03, %eax
  mov %eax, pdpt+32
  movl $0, pdpt+36
  mov $pd5, %eax
  or $0x03, %eax
  mov %eax, pdpt+40
  movl $0, pdpt+44
  mov $pd6, %eax
  or $0x03, %eax
  mov %eax, pdpt+48
  movl $0, pdpt+52
  mov $pd7, %eax
  or $0x03, %eax
  mov %eax, pdpt+56
  movl $0, pdpt+60
  mov $pd8, %eax
  or $0x03, %eax
  mov %eax, pdpt+64
  movl $0, pdpt+68
  mov $pd9, %eax
  or $0x03, %eax
  mov %eax, pdpt+72
  movl $0, pdpt+76
  mov $pd10, %eax
  or $0x03, %eax
  mov %eax, pdpt+80
  movl $0, pdpt+84
  mov $pd11, %eax
  or $0x03, %eax
  mov %eax, pdpt+88
  movl $0, pdpt+92
  mov $pd12, %eax
  or $0x03, %eax
  mov %eax, pdpt+96
  movl $0, pdpt+100
  mov $pd13, %eax
  or $0x03, %eax
  mov %eax, pdpt+104
  movl $0, pdpt+108
  mov $pd14, %eax
  or $0x03, %eax
  mov %eax, pdpt+112
  movl $0, pdpt+116
  mov $pd15, %eax
  or $0x03, %eax
  mov %eax, pdpt+120
  movl $0, pdpt+124
  mov $'3', %al
  outb %al, %dx
  mov $'\n', %al
  outb %al, %dx

  // Fill PDs with 2MiB identity maps for first 16GiB
  mov $0, %ebx            // pd index (0..15)
  mov $pd0, %edi          // current PD
.fill_pd_outer:
  mov $0, %esi            // entry index (0..511)
.fill_pd_inner:
  mov %ebx, %eax
  shl $9, %eax            // pd_index * 512
  add %esi, %eax
  shl $21, %eax           // * 2MiB
  or $0x83, %eax
  mov %eax, (%edi)
  movl $0, 4(%edi)
  add $8, %edi
  inc %esi
  cmp $512, %esi
  jne .fill_pd_inner
  inc %ebx
  cmp $16, %ebx
  jne .fill_pd_outer
  mov $'4', %al
  outb %al, %dx
  mov $'\n', %al
  outb %al, %dx

  // Enable PAE
  mov %cr4, %eax
  or $0x20, %eax
  mov %eax, %cr4
  mov $'5', %al
  outb %al, %dx
  mov $'\n', %al
  outb %al, %dx

  // Load CR3
  mov $pml4, %eax
  mov %eax, %cr3
  mov $'6', %al
  outb %al, %dx
  mov $'\n', %al
  outb %al, %dx

  // Enable LME in EFER
  mov $0xC0000080, %ecx
  rdmsr
  or $0x100, %eax
  wrmsr
  mov $'7', %al
  outb %al, %dx
  mov $'\n', %al
  outb %al, %dx

  // Enable paging
  mov %cr0, %eax
  or $0x80000000, %eax
  mov %eax, %cr0
  mov $'8', %al
  outb %al, %dx
  mov $'\n', %al
  outb %al, %dx

  // Load 64-bit GDT and jump
  lgdt gdt64_ptr
  ljmp $0x08, $grub_long_mode

  .code64
grub_long_mode:
  mov $0x10, %ax
  mov %ax, %ds
  mov %ax, %es
  mov %ax, %ss
  mov %ax, %fs
  mov %ax, %gs
  // Debugcon marker after long mode switch
  mov $0x00E9, %dx
  mov $'L', %al
  outb %al, %dx
  mov $'M', %al
  outb %al, %dx
  mov $'\n', %al
  outb %al, %dx
  // Jump to kernel start (boot.s sets its own stack)
  jmp _start

  .section .rodata
gdt64:
  .quad 0x0000000000000000
  .quad 0x00AF9A000000FFFF
  .quad 0x00AF92000000FFFF
gdt64_ptr:
  .word (gdt64_end - gdt64 - 1)
  .long gdt64
gdt64_end:

  .bss
  .align 16
stack32:
  .skip 4096
stack32_top:

  .section .data
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

  .align 4096
pml4:
  .skip 4096
pdpt:
  .skip 4096
pd0:
  .skip 4096
pd1:
  .skip 4096
pd2:
  .skip 4096
pd3:
  .skip 4096
pd4:
  .skip 4096
pd5:
  .skip 4096
pd6:
  .skip 4096
pd7:
  .skip 4096
pd8:
  .skip 4096
pd9:
  .skip 4096
pd10:
  .skip 4096
pd11:
  .skip 4096
pd12:
  .skip 4096
pd13:
  .skip 4096
pd14:
  .skip 4096
pd15:
  .skip 4096
