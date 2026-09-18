// runtime/boot/x86_64/exceptions.S
// Minimal exception stubs (0..31) that print a debug marker and halt.
// We avoid returning to prevent triple-fault reboot loops during bring-up.

  .text

// --- helpers -------------------------------------------------------
hex_nib:
  cmp %al, 0x0A
  jb .hex_num
  add $'A' - 10, %al
  jmp .hex_emit
.hex_num:
  add $'0', %al
.hex_emit:
  outb %al, $0xE9
  ret

hex_out:
  push %rax
  mov %al, %ah
  shr $4, %al
  call hex_nib
  mov %ah, %al
  and $0x0F, %al
  call hex_nib
  pop %rax
  ret

isr_ex_common:
  cli
  mov $'E', %al
  outb %al, $0xE9
  mov (%rsp), %al      // vector
  call hex_out
  mov $' ', %al
  outb %al, $0xE9
  mov 8(%rsp), %al     // error code low byte
  call hex_out
.hang:
  hlt
  jmp .hang

// --- macros --------------------------------------------------------
  .macro ISR_NOERR n
    .global isr_ex\n
  isr_ex\n:
    pushq $0
    pushq $\n
    jmp isr_ex_common
  .endm

  .macro ISR_ERR n
    .global isr_ex\n
  isr_ex\n:
    pushq $\n
    jmp isr_ex_common
  .endm

// --- exception stubs (0..31) --------------------------------------
  ISR_NOERR 0
  ISR_NOERR 1
  ISR_NOERR 2
  ISR_NOERR 3
  ISR_NOERR 4
  ISR_NOERR 5
  ISR_NOERR 6
  ISR_NOERR 7
  ISR_ERR   8
  ISR_NOERR 9
  ISR_ERR   10
  ISR_ERR   11
  ISR_ERR   12
  ISR_ERR   13
  ISR_ERR   14
  ISR_NOERR 15
  ISR_NOERR 16
  ISR_ERR   17
  ISR_NOERR 18
  ISR_NOERR 19
  ISR_NOERR 20
  ISR_ERR   21
  ISR_NOERR 22
  ISR_NOERR 23
  ISR_NOERR 24
  ISR_NOERR 25
  ISR_NOERR 26
  ISR_NOERR 27
  ISR_NOERR 28
  ISR_NOERR 29
  ISR_ERR   30
  ISR_NOERR 31
