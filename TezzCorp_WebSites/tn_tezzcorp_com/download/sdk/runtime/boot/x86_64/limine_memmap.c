// runtime/boot/x86_64/limine_memmap.c
// Limine memory map request for OS bring-up.

#include "../limine/limine.h"

__attribute__((section(".limine_requests"), used))
volatile struct limine_memmap_request limine_memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
    .response = 0
};
