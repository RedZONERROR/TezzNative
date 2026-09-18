// runtime/boot/x86_64/boot.s
// Tiny 64-bit entry stub (expects long mode). Sets stack and jumps to tn_kernel_main.

  .text
  .global _start
  .extern tn_kernel_main
  .extern __bss_start
  .extern __bss_end
  .extern limine_fb_marker
  .extern tn_fb_boot_marker

_start:
  cli
  /* Pre-entry debugcon marker before any C/Tezz code. */
  mov $0x00E9, %dx
  mov $'P', %al
  outb %al, %dx
  mov $'R', %al
  outb %al, %dx
  mov $'E', %al
  outb %al, %dx
  mov $'\n', %al
  outb %al, %dx
  lea stack_top(%rip), %rsp
  mov %cr0, %rax
  and $-5, %rax
  or $2, %rax
  mov %rax, %cr0
  mov %cr4, %rax
  or $0x600, %rax
  mov %rax, %cr4
  fninit
  mov $0x00E9, %dx
  mov $'S', %al
  outb %al, %dx
  // Zero .bss before running C/Tezz runtime.
  lea __bss_end(%rip), %rcx
  mov $0x00E9, %dx
  mov $'1', %al
  outb %al, %dx
  lea __bss_start(%rip), %rdi
  mov $0x00E9, %dx
  mov $'2', %al
  outb %al, %dx
  // Guard against bogus BSS ranges to avoid faulting before we can log.
  // Skip zeroing only for invalid ranges.
  cmp %rdi, %rcx
  jbe .bss_skip
  mov $0x00E9, %dx
  mov $'3', %al
  outb %al, %dx
  sub %rdi, %rcx
  cmp $0, %rcx
  jle .bss_done
  mov $0x00E9, %dx
  mov $'4', %al
  outb %al, %dx
  // If BSS size is unexpectedly huge, skip zeroing for safety.
  cmp $0x00800000, %rcx
  ja .bss_skip
  cld
  xor %rax, %rax
  rep stosb
  jmp .bss_done
.bss_skip:
  mov $0x00E9, %dx
  mov $'b', %al
  outb %al, %dx
.bss_done:
  mov $0x00E9, %dx
  mov $'Z', %al
  outb %al, %dx
  // Early kernel marker (debugcon + VGA)
  lea msg_kdebug(%rip), %rsi
  call debugcon_write
  lea msg_kvga(%rip), %rsi
  call vga_write_str
  // Also place TEZZKERN on the next text row for visibility.
  mov $0xB8000, %rdi
  add $160, %rdi
  lea msg_kvga(%rip), %rsi
  call vga_write_str_at
#ifdef TEZZ_VERIFY
  // Exit QEMU quickly in verify runs (isa-debug-exit on port 0xF4).
  mov $0x00F4, %dx
  mov $0x10, %al
  outb %al, %dx
#endif
  // Early COM1 serial init + marker before paging/Tezz runtime.
  call serial_init
  mov $0x00E9, %dx
  mov $'I', %al
  outb %al, %dx
  lea msg_kserial(%rip), %rsi
  call serial_write_str
  lea msg_serial(%rip), %rsi
  call serial_write_str
  mov $0x00E9, %dx
  mov $'s', %al
  outb %al, %dx
  // Debugcon marker (port 0xE9) + string
  lea msg_debugcon(%rip), %rsi
  call debugcon_write
  mov $0x00E9, %dx
  mov $'T', %al
  outb %al, %dx
  // VGA text fallback (if text mode is active)
  lea msg_vga(%rip), %rsi
  call vga_write_str
  mov $0x00E9, %dx
  mov $'V', %al
  outb %al, %dx
  // Limine framebuffer marker (if available)
  call limine_fb_marker
  mov $0x00E9, %dx
  mov $'F', %al
  outb %al, %dx
  // Generic framebuffer marker (Limine or GRUB fb)
  // call tn_fb_boot_marker
  mov $0x00E9, %dx
  mov $'G', %al
  outb %al, %dx
  // Windows x64 ABI: RSP%16=8 before call; reserve 32B shadow space.
  and $-16, %rsp
  sub $8, %rsp
  sub $32, %rsp
  mov $0x00E9, %dx
  mov $'c', %al
  outb %al, %dx
  call tn_kernel_main
  add $32, %rsp
  add $8, %rsp
  mov $0x00E9, %dx
  mov $'r', %al
  outb %al, %dx
.halt:
  hlt
  jmp .halt

  .bss
  .align 16
stack:
  .skip 65536
stack_top:

  .text
// Minimal COM1 serial init (0x3F8) and output.
serial_init:
  mov $0x00E9, %dx
  mov $'A', %al
  outb %al, %dx
  mov $0x3F8, %dx
  mov $0x00, %al      # disable interrupts
  outb %al, %dx
  mov $0x3F9, %dx
  outb %al, %dx
  mov $0x3FB, %dx     # line control: enable DLAB
  mov $0x80, %al
  outb %al, %dx
  mov $0x3F8, %dx     # divisor low
  mov $0x03, %al
  outb %al, %dx
  mov $0x3F9, %dx     # divisor high
  mov $0x00, %al
  outb %al, %dx
  mov $0x3FB, %dx     # 8N1
  mov $0x03, %al
  outb %al, %dx
  mov $0x00E9, %dx
  mov $'B', %al
  outb %al, %dx
  ret

serial_putc:
  // wait for THR empty (LSR bit 5)
  mov %al, %bl
  mov $0x3FD, %dx
.sp_wait:
  inb %dx, %al
  test $0x20, %al
  jz .sp_wait
  mov $0x3F8, %dx
  mov %bl, %al
  outb %al, %dx
  ret

serial_write_str:
  // %rsi = cstr
  mov %rsi, %rdi
.sw_loop:
  movb (%rdi), %al
  test %al, %al
  jz .sw_done
  call serial_putc
  inc %rdi
  jmp .sw_loop
.sw_done:
  ret

debugcon_write:
  // %rsi = cstr, port 0xE9
  mov $0x00E9, %dx
.dc_loop:
  movb (%rsi), %al
  test %al, %al
  jz .dc_done
  outb %al, %dx
  inc %rsi
  jmp .dc_loop
.dc_done:
  ret

  .section .rodata
msg_debugcon:
  .ascii "TEZZBOOT\n\0"
msg_serial:
  .ascii "TEZZSER\n\0"
msg_vga:
  .ascii "TEZZBOOT\n\0"
msg_kdebug:
  .ascii "TEZZKERN\n\0"
msg_kserial:
  .ascii "TEZZKERN\n\0"
msg_kvga:
  .ascii "TEZZKERN\n\0"

vga_write_str:
  // %rsi = cstr, write to VGA text buffer at 0xB8000 (green on black)
  mov $0xB8000, %rdi
.vga_loop:
  movb (%rsi), %al
  test %al, %al
  jz .vga_done
  mov $0x0A, %ah
  movw %ax, (%rdi)
  add $2, %rdi
  inc %rsi
  jmp .vga_loop
.vga_done:
  ret

vga_write_str_at:
  // %rdi = VGA dest, %rsi = cstr
.vga2_loop:
  movb (%rsi), %al
  test %al, %al
  jz .vga2_done
  mov $0x0A, %ah
  movw %ax, (%rdi)
  add $2, %rdi
  inc %rsi
  jmp .vga2_loop
.vga2_done:
  ret
