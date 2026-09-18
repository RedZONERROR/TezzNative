// runtime/boot/x86/irq.S
// 32-bit IRQ/exception stubs for PIC-driven PIT + keyboard.

  .text
  .code32
  .global isr_irq0
  .global isr_irq1
  .global isr_irq12
  .global isr_ex0
  .global isr_ex14
  .extern tn_irq0_handler
  .extern tn_irq1_handler
  .extern tn_irq12_handler
  .extern tn_ex0_handler
  .extern tn_ex14_handler

isr_irq0:
  pusha
  call tn_irq0_handler
  popa
  iret

isr_irq1:
  pusha
  call tn_irq1_handler
  popa
  iret

isr_irq12:
  pusha
  call tn_irq12_handler
  popa
  iret

isr_ex0:
  cli
  pusha
  call tn_ex0_handler
  popa
  iret

isr_ex14:
  cli
  pusha
  // Stack on #PF: err, eip, cs, eflags; after pusha err is at +32.
  mov 32(%esp), %eax
  push %eax
  call tn_ex14_handler
  add $4, %esp
  popa
  add $4, %esp
  iret
