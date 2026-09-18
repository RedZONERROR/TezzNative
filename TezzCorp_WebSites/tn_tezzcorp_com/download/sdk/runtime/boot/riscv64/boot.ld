/* runtime/boot/riscv64/boot.ld */
OUTPUT_FORMAT("elf64-littleriscv")
OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
  . = 0x80200000;

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
