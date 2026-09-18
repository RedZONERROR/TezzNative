// runtime/boot/x86_64/limine_entry.c
// Minimal Limine entry request to jump into our _start.

#include "../limine/limine.h"

extern void _start(void);
extern void limine_raw_entry(void);
extern volatile uint64_t limine_base_revision[];

static inline void debugcon_putc(char c){
    __asm__ volatile("outb %0, %1" : : "a"(c), "Nd"(0x00E9));
}

static void debugcon_puts(const char *s){
    if(!s) return;
    while(*s){
        debugcon_putc(*s++);
    }
}

static inline void outb_u8(unsigned short port, unsigned char v){
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

static inline unsigned char inb_u8(unsigned short port){
    unsigned char v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void serial_init(void){
    unsigned short base = 0x3F8;
    outb_u8(base + 1, 0x00);
    outb_u8(base + 3, 0x80);
    outb_u8(base + 0, 0x03);
    outb_u8(base + 1, 0x00);
    outb_u8(base + 3, 0x03);
    outb_u8(base + 2, 0xC7);
    outb_u8(base + 4, 0x0B);
}

static void serial_putc(char c){
    unsigned short base = 0x3F8;
    for(int i=0;i<100000;i++){
        if(inb_u8(base + 5) & 0x20) break;
    }
    outb_u8(base, (unsigned char)c);
}

static void serial_puts(const char *s){
    if(!s) return;
    while(*s){
        serial_putc(*s++);
    }
}

static void limine_entry_shim(void){
    serial_init();
    serial_puts("LIMINE_SER\n");
    if(LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)){
        debugcon_puts("LIMINE_OK\n");
        serial_puts("LIMINE_OK\n");
    }else{
        debugcon_puts("LIMINE_BAD\n");
        serial_puts("LIMINE_BAD\n");
    }
    limine_raw_entry();
}

__attribute__((section(".limine_requests_start"), used))
volatile uint64_t limine_requests_start[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((section(".limine_requests"), used))
volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(0);

__attribute__((section(".limine_requests"), used))
volatile struct limine_entry_point_request limine_entry_request = {
    .id = LIMINE_ENTRY_POINT_REQUEST_ID,
    .revision = 0,
    .response = 0,
    .entry = limine_entry_shim
};

__attribute__((section(".limine_requests"), used))
volatile struct limine_executable_file_request tezz_limine_executable_file_request = {
    .id = LIMINE_EXECUTABLE_FILE_REQUEST_ID,
    .revision = 0,
    .response = 0
};

__attribute__((section(".limine_requests_end"), used))
volatile uint64_t limine_requests_end[] = LIMINE_REQUESTS_END_MARKER;
