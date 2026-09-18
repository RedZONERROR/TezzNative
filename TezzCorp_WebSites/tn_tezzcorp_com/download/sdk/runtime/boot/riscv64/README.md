# riscv64 Boot + HAL Scaffold

This directory is the first bring-up scaffold for TezzOS on `riscv64` (QEMU `virt` profile).

Contents:
- `entry.S` — minimal `_start` that clears `.bss`, sets a stack, and calls `tn_kernel_main`.
- `hal.c` — architecture stubs for `arch_load_cr3`, `arch_enable_paging`, `arch_invlpg`, `arch_rdtsc`.
- `boot.ld` — minimal linker layout for a `riscv64` freestanding ELF image.

Notes:
- This is intentionally minimal and does not yet include page-table, trap, or SBI setup.
- Use `tools/boot_matrix.sh` to generate matrix artifacts and validate scaffold wiring.
