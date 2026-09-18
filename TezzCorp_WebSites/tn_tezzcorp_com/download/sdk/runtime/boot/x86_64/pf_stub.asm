; Minimal x86_64 IDT + page-fault handler stub (NASM syntax)
; Load with lidt, then enable paging. Handler just halts.

BITS 64

section .data
align 16
idt:
  times 256 dq 0, 0

times 1 dq 0

section .text
global idt_load

idt_load:
  lea rax, [rel idt]
  mov word [rel idtr], 4096-1
  mov qword [rel idtr+2], rax
  lidt [rel idtr]
  ret

global pf_handler
pf_handler:
  cli
.hang:
  hlt
  jmp .hang

section .data
idtr:
  dw 0
  dq 0
