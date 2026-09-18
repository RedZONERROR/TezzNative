  .section .rodata
  .global tn_hal_abi_tag
tn_hal_abi_tag:
  .long 1413831258
  .long 8
  .long 2

  .text
  .global tn_hal_abi_magic
tn_hal_abi_magic:
  mov $1413831258, %eax
  ret

  .global tn_hal_abi_word_bytes
tn_hal_abi_word_bytes:
  mov $8, %eax
  ret

  .global tn_hal_abi_callconv_id
tn_hal_abi_callconv_id:
  mov $2, %eax
  ret
