// runtime/boot/x86_64/acpi.c
// Minimal ACPI parser for HPET base discovery (Limine RSDP).

#include "../limine/limine.h"
#include <stdint.h>
#include <stddef.h>

#if defined(__x86_64__) && (defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__) || defined(TN_WINABI) || defined(TEZZ_UEFI))
#define TN_MSABI __attribute__((ms_abi))
#else
#define TN_MSABI
#endif

__attribute__((section(".limine_requests"), used))
volatile struct limine_rsdp_request limine_rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID,
    .revision = 0,
    .response = 0
};

static int memeq(const char* a, const char* b, size_t n){
  for(size_t i=0;i<n;i++){
    if(a[i] != b[i]) return 0;
  }
  return 1;
}

struct acpi_rsdp {
  char signature[8];
  uint8_t checksum;
  char oemid[6];
  uint8_t revision;
  uint32_t rsdt_address;
  uint32_t length;
  uint64_t xsdt_address;
  uint8_t ext_checksum;
  uint8_t reserved[3];
} __attribute__((packed));

struct acpi_sdt_header {
  char signature[4];
  uint32_t length;
  uint8_t revision;
  uint8_t checksum;
  char oemid[6];
  char oem_table_id[8];
  uint32_t oem_revision;
  uint32_t creator_id;
  uint32_t creator_revision;
} __attribute__((packed));

struct acpi_gas {
  uint8_t address_space_id;
  uint8_t register_bit_width;
  uint8_t register_bit_offset;
  uint8_t access_size;
  uint64_t address;
} __attribute__((packed));

struct acpi_hpet {
  struct acpi_sdt_header header;
  uint32_t event_timer_block_id;
  struct acpi_gas base_address;
  uint8_t hpet_number;
  uint16_t min_tick;
  uint8_t page_protection;
} __attribute__((packed));

static uint64_t acpi_find_hpet(uint64_t sdt_addr, int xsdt){
  if(sdt_addr == 0) return 0;
  struct acpi_sdt_header* sdt = (struct acpi_sdt_header*)(uintptr_t)sdt_addr;
  if(!sdt) return 0;
  if(xsdt && !memeq(sdt->signature, "XSDT", 4)) return 0;
  if(!xsdt && !memeq(sdt->signature, "RSDT", 4)) return 0;
  if(sdt->length < sizeof(struct acpi_sdt_header)) return 0;
  uint32_t entries = (sdt->length - sizeof(struct acpi_sdt_header)) / (xsdt ? 8u : 4u);
  uint8_t* ptr = (uint8_t*)sdt + sizeof(struct acpi_sdt_header);
  for(uint32_t i=0;i<entries;i++){
    uint64_t tbl = xsdt ? ((uint64_t*)ptr)[i] : (uint64_t)((uint32_t*)ptr)[i];
    if(!tbl) continue;
    struct acpi_sdt_header* th = (struct acpi_sdt_header*)(uintptr_t)tbl;
    if(th && memeq(th->signature, "HPET", 4)){
      struct acpi_hpet* hpet = (struct acpi_hpet*)th;
      if(hpet->base_address.address_space_id != 0) continue;
      return hpet->base_address.address;
    }
  }
  return 0;
}

TN_MSABI long long os__acpi_hpet_base(void){
#ifdef TN_LIMINE
  if(!limine_rsdp_request.response) return 0;
  if(!limine_rsdp_request.response->address) return 0;
  struct acpi_rsdp* rsdp = (struct acpi_rsdp*)(uintptr_t)limine_rsdp_request.response->address;
  if(!rsdp) return 0;
  if(!memeq(rsdp->signature, "RSD PTR ", 8)) return 0;
  if(rsdp->revision >= 2 && rsdp->xsdt_address){
    return (long long)acpi_find_hpet(rsdp->xsdt_address, 1);
  }
  return (long long)acpi_find_hpet((uint64_t)rsdp->rsdt_address, 0);
#else
  return 0;
#endif
}
