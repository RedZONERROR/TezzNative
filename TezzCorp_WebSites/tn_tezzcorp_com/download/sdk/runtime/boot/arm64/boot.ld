/* runtime/boot/arm64/boot.ld */
OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(_start)

SECTIONS
{
  . = 0x40080000;

  .text : {
    *(.text.boot)
    *(.text*)
  }

  .rodata : { *(.rodata*) }
  .data : { *(.data*) }

  .bss (NOLOAD) : {
    __bss_start = .;
    *(.bss*)
    *(.bss.stack)
    *(COMMON)
    __bss_end = .;
  }
}
