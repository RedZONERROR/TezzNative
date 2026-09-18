// runtime/boot/x86_64/limine_raw.S
// Raw Limine entry: writes a marker to debugcon (0xE9) before any C/Tezz code.

  .text
  .global limine_raw_entry
limine_raw_entry:
  /* Init COM1 (0x3F8) and emit RAW marker on serial before any C. */
  mov $0x3F8, %dx
  xor %al, %al
  outb %al, %dx          /* disable interrupts */
  mov $0x3F9, %dx
  outb %al, %dx
  mov $0x3FB, %dx
  mov $0x80, %al
  outb %al, %dx          /* enable DLAB */
  mov $0x3F8, %dx
  mov $0x03, %al
  outb %al, %dx          /* divisor low (38400 baud) */
  mov $0x3F9, %dx
  xor %al, %al
  outb %al, %dx          /* divisor high */
  mov $0x3FB, %dx
  mov $0x03, %al
  outb %al, %dx          /* 8N1 */

  /* serial_putc('R') */
  mov $0x3FD, %dx
1:inb %dx, %al
  test $0x20, %al
  jz 1b
  mov $0x3F8, %dx
  mov $'R', %al
  outb %al, %dx
  /* serial_putc('A') */
  mov $0x3FD, %dx
2:inb %dx, %al
  test $0x20, %al
  jz 2b
  mov $0x3F8, %dx
  mov $'A', %al
  outb %al, %dx
  /* serial_putc('W') */
  mov $0x3FD, %dx
3:inb %dx, %al
  test $0x20, %al
  jz 3b
  mov $0x3F8, %dx
  mov $'W', %al
  outb %al, %dx
  /* serial_putc('\\n') */
  mov $0x3FD, %dx
4:inb %dx, %al
  test $0x20, %al
  jz 4b
  mov $0x3F8, %dx
  mov $'\n', %al
  outb %al, %dx

  /* Debugcon marker too (0xE9). */
  mov $0x00E9, %dx
  mov $'R', %al
  outb %al, %dx
  mov $'A', %al
  outb %al, %dx
  mov $'W', %al
  outb %al, %dx
  mov $'\n', %al
  outb %al, %dx
  jmp _start
