/* runtime/boot/x86_64/boot.ld */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

KERNEL_VMA = 0xffffffff80000000;
KERNEL_LMA = 0x00100000;

PHDRS
{
  limine PT_LOAD FLAGS(4); /* R */
  text   PT_LOAD FLAGS(5); /* R|X */
  data   PT_LOAD FLAGS(6); /* R|W */
}

SECTIONS
{
  . = KERNEL_VMA + KERNEL_LMA;
  .limine_requests ALIGN(4K) : AT(ADDR(.limine_requests) - KERNEL_VMA) {
    KEEP(*(.limine_requests_start))
    KEEP(*(.limine_requests))
    KEEP(*(.limine_requests_end))
  } :limine
  .text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VMA) {
    /* Place multiboot2 header at start of .text so it's within first 32KB. */
    KEEP(*(.multiboot2))
    *(.text*)
  } :text
  .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_VMA) { *(.rodata*) } :text
  .data ALIGN(4K) : AT(ADDR(.data) - KERNEL_VMA) { *(.data*) } :data
  .bss ALIGN(4K) (NOLOAD) : {
    __bss_start = .;
    *(.bss*) *(COMMON)
    __bss_end = .;
  } :data
}
