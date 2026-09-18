/* runtime/boot/x86/boot.ld */
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
  . = 0x00100000;

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
