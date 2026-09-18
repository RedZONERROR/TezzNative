# x86_64 Boot Stubs

- `paging.S`: arch helpers `arch_load_cr3` + `arch_enable_paging` used by `lib/os.tn`.
- `boot.s`: tiny long?mode entry stub that calls `tn_kernel_main`.

These are templates. Integrate with your loader (UEFI, Limine, GRUB, etc.) and
ensure you enter long mode before `_start`.

See also:
- `runtime/boot/x86` for the first 32-bit scaffold path.
