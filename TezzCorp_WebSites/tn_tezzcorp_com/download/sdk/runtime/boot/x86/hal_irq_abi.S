  .section .rodata
  .global tn_hal_irq_abi_tag
tn_hal_irq_abi_tag:
  .long 1414091345
  .long 4
  .long 1

  .text
  .global tn_hal_irq_abi_magic
tn_hal_irq_abi_magic:
  mov $1414091345, %eax
  ret

  .global tn_hal_irq_abi_word_bytes
tn_hal_irq_abi_word_bytes:
  mov $4, %eax
  ret

  .global tn_hal_irq_abi_callconv_id
tn_hal_irq_abi_callconv_id:
  mov $1, %eax
  ret
