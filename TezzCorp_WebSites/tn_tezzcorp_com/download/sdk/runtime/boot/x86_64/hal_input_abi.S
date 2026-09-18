  .section .rodata
  .global tn_hal_input_abi_tag
tn_hal_input_abi_tag:
  .long 1414090320
  .long 8
  .long 2

  .text
  .global tn_hal_input_abi_magic
tn_hal_input_abi_magic:
  mov $1414090320, %eax
  ret

  .global tn_hal_input_abi_word_bytes
tn_hal_input_abi_word_bytes:
  mov $8, %eax
  ret

  .global tn_hal_input_abi_callconv_id
tn_hal_input_abi_callconv_id:
  mov $2, %eax
  ret
