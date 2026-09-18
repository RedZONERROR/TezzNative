/* runtime/boot/x86/boot_grub.ld */
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(grub_entry)

SECTIONS
{
  . = 0x00100000;
  .text ALIGN(4K) : {
    KEEP(*(.multiboot2))
    *(.text.boot)
    *(.text*)
  }
  .rodata ALIGN(4K) : { *(.rodata*) }
  .data ALIGN(4K) : { *(.data*) }
  .bss ALIGN(4K) (NOLOAD) : {
    __bss_start = .;
    *(.bss*)
    *(.bss.stack)
    *(COMMON)
    __bss_end = .;
  }
}
