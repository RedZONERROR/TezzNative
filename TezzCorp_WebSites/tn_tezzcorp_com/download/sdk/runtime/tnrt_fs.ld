/* runtime/tnrt_fs.ld - minimal bare-metal linker script template */
ENTRY(_start)

SECTIONS
{
  . = 0x200000;

  .text : {
    *(.text*)
  }

  .rodata : {
    *(.rodata*)
  }

  .data : {
    *(.data*)
  }

  .bss : {
    *(.bss*)
    *(COMMON)
  }
}
