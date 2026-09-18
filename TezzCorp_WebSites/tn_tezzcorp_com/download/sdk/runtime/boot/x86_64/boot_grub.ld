/* runtime/boot/x86_64/boot_grub.ld */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(grub_entry)

PHDRS
{
  limine PT_LOAD FLAGS(4); /* R */
  text   PT_LOAD FLAGS(5); /* R|X */
  data   PT_LOAD FLAGS(6); /* R|W */
}

SECTIONS
{
  . = 1M;
  .limine_requests : ALIGN(4K) {
    KEEP(*(.limine_requests_start))
    KEEP(*(.limine_requests))
    KEEP(*(.limine_requests_end))
  } :limine
  .text : ALIGN(4K) {
    /* Place multiboot2 header at start of .text so it's within first 32KB. */
    KEEP(*(.multiboot2))
    *(.text*)
  } :text
  .rodata : ALIGN(4K) { *(.rodata*) } :text
  .data : ALIGN(4K) { *(.data*) } :data
  .bss : ALIGN(4K) {
    __bss_start = .;
    *(.bss*) *(COMMON)
    __bss_end = .;
  } :data
}
