// runtime/boot/x86_64/irq.S
// IRQ stubs for PIC-driven timer/keyboard (x86_64).

  .text
  .global isr_irq0
  .global isr_irq1
  .global isr_irq12
  .extern tn_irq0_handler
  .extern tn_irq1_handler
  .extern tn_irq12_handler

.macro PUSH_REGS
  pushq %rax
  pushq %rcx
  pushq %rdx
  pushq %rbx
  pushq %rbp
  pushq %rsi
  pushq %rdi
  pushq %r8
  pushq %r9
  pushq %r10
  pushq %r11
  pushq %r12
  pushq %r13
  pushq %r14
  pushq %r15
.endm

.macro POP_REGS
  popq %r15
  popq %r14
  popq %r13
  popq %r12
  popq %r11
  popq %r10
  popq %r9
  popq %r8
  popq %rdi
  popq %rsi
  popq %rbp
  popq %rbx
  popq %rdx
  popq %rcx
  popq %rax
.endm

isr_irq0:
  PUSH_REGS
  sub $8, %rsp
  call tn_irq0_handler
  add $8, %rsp
  POP_REGS
  iretq

isr_irq1:
  PUSH_REGS
  sub $8, %rsp
  call tn_irq1_handler
  add $8, %rsp
  POP_REGS
  iretq

isr_irq12:
  PUSH_REGS
  sub $8, %rsp
  call tn_irq12_handler
  add $8, %rsp
  POP_REGS
  iretq
