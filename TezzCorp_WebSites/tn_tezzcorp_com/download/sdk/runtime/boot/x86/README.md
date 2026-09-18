# x86 Boot + HAL Scaffold

This directory is the first bring-up scaffold for TezzOS on `x86` (32-bit).

Contents:
- `entry.S` — minimal `_start` that clears `.bss`, sets a stack, and calls `tn_kernel_main`.
- `irq.c` + `irq.S` — 32-bit IDT bring-up with PIC/PIT timer + PS/2 keyboard IRQ handling.
- `hal.c` — architecture helpers for `arch_load_cr3`, `arch_enable_paging`, `arch_invlpg`, `arch_rdtsc`.
- `builtins.c` — freestanding 64-bit div/mod helper builtins for 32-bit link.
- `boot.ld` — minimal linker layout for an `x86` freestanding ELF image.
- `grub_mb2.S` + `boot_grub.ld` — multiboot2 entry/link path for GRUB ISO boot.

Notes:
- This path now includes real x86 PIC/PIT/keyboard interrupt plumbing; APIC remains stubbed.
- Use `tools/boot_matrix.sh` to validate the matrix path and scaffold integrity.
- Native boot/verify:
  - `TEZZ_BOOT_ARCH=x86 TEZZ_GRUB_REBUILD=1 bash tools/grub_build.sh`
  - `TEZZ_BOOT_ARCH=x86 bash tools/grub_verify.sh`
