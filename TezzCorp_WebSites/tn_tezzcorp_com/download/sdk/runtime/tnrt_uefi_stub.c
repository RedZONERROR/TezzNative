// runtime/tnrt_uefi_stub.c
// Minimal UEFI entry stub (EDK2-style). This file is a template.
// It is not compiled by default; integrate with your UEFI build system.

#include <stdint.h>

// Forward-declare TezzNative kernel entry.
extern int tn_kernel_main(void);

// UEFI entry (signature varies by toolchain); adapt to your environment.
// For GNU-EFI/EDK2, the signature is typically:
// EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable)
int tn_uefi_entry(void* image_handle, void* system_table){
  (void)image_handle;
  (void)system_table;
  return tn_kernel_main();
}
