  .section .rodata
  .global tn_hal_memory_abi_tag
tn_hal_memory_abi_tag:
  .long 1414350157
  .long 4
  .long 1

  .text
  .global tn_hal_memory_abi_magic
tn_hal_memory_abi_magic:
  mov $1414350157, %eax
  ret

  .global tn_hal_memory_abi_word_bytes
tn_hal_memory_abi_word_bytes:
  mov $4, %eax
  ret

  .global tn_hal_memory_abi_callconv_id
tn_hal_memory_abi_callconv_id:
  mov $1, %eax
  ret
