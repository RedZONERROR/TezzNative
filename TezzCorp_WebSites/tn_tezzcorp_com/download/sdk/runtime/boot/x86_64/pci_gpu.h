#pragma once
#include <stdint.h>

// PCI Configuration Mechanism #1 ports.
// Reference: Linux PCI config access conventions and x86 PCI config I/O.
#define TN_PCI_CFG_ADDR_PORT 0xCF8u
#define TN_PCI_CFG_DATA_PORT 0xCFCu

// PCI class codes.
#define TN_PCI_CLASS_DISPLAY 0x03u
#define TN_PCI_SUBCLASS_VGA  0x00u
#define TN_PCI_CLASS_NETWORK 0x02u
#define TN_PCI_CLASS_MULTIMEDIA 0x04u
#define TN_PCI_SUBCLASS_AUDIO_DEV 0x01u
#define TN_PCI_SUBCLASS_AUDIO_HDA 0x03u

// VMware SVGA PCI IDs.
// Reference: Linux vmwgfx (VMWGFX_PCI_ID_SVGA2/SVGA3).
#define TN_PCI_VENDOR_VMWARE      0x15ADu
#define TN_PCI_DEVICE_VMWARE_SVGA2 0x0405u
#define TN_PCI_DEVICE_VMWARE_SVGA3 0x0406u

// Legacy VBox VGA ID (fallback ranking).
#define TN_PCI_VENDOR_VBOX        0x80EEu
#define TN_PCI_DEVICE_VBOX_VGA    0xBEEFu

// Bochs/QEMU stdvga compatible IDs.
#define TN_PCI_VENDOR_BOCHS_QEMU  0x1234u
#define TN_PCI_DEVICE_BOCHS_VGA   0x1111u

// VMware SVGA register interface.
// Reference: vmwgfx device_include/svga_reg.h.
#define TN_SVGA_INDEX_PORT 0x0u
#define TN_SVGA_VALUE_PORT 0x1u

#define TN_SVGA_REG_ID           0u
#define TN_SVGA_REG_CAPABILITIES 17u
#define TN_SVGA_REG_CURSOR_X     25u
#define TN_SVGA_REG_CURSOR_Y     26u
#define TN_SVGA_REG_CURSOR_ON    27u
#define TN_SVGA_REG_CURSOR4_ON   70u
#define TN_SVGA_REG_CURSOR4_X    71u
#define TN_SVGA_REG_CURSOR4_Y    72u

#define TN_SVGA_CURSOR_ON_HIDE 0x0u
#define TN_SVGA_CURSOR_ON_SHOW 0x1u

#define TN_SVGA_CAP_CURSOR          0x00000020u
#define TN_SVGA_CAP_CURSOR_BYPASS   0x00000040u
#define TN_SVGA_CAP_CURSOR_BYPASS_2 0x00000080u

typedef struct tn_gpu_pci_state {
  uint8_t present;
  uint8_t bus;
  uint8_t slot;
  uint8_t func;
  uint16_t vendor;
  uint16_t device;
  uint8_t class_code;
  uint8_t subclass;
  uint8_t prog_if;
  uint8_t header_type;
  uint16_t command;
  uint64_t bar_addr[6];
  uint64_t bar_size[6];
  uint8_t bar_is_io[6];
  uint8_t bar_prefetch[6];
  uint8_t bar_mem_type[6];
  uint8_t bar_safe[6];
  uint8_t mmio_safe;
  uint16_t svga_io_base;
  uint32_t svga_caps;
  uint8_t svga_cursor_ready;
  uint8_t svga_cursor4;
  uint8_t svga_cursor_on;
} tn_gpu_pci_state;
