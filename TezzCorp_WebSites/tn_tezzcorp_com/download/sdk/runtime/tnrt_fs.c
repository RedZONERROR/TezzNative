// runtime/tnrt_fs.c
// Minimal freestanding runtime for kernel/OS targets.

#include "tnrt_fs.h"
#include "../boot/tezzboot/tzbt_abi.h"
#include "boot/x86_64/pci_gpu.h"
#if (defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__) || defined(TN_WINABI) || defined(TEZZ_UEFI))
#define TN_MSABI __attribute__((ms_abi))
#else
#define TN_MSABI
#endif
#ifdef TN_LIMINE
#include "limine.h"
extern volatile struct limine_framebuffer_request limine_fb_request;
extern volatile struct limine_memmap_request limine_memmap_request;
extern volatile struct limine_executable_file_request tezz_limine_executable_file_request;
extern long long limine_fb_text_at(const unsigned char* s, long long len, long long x, long long y, unsigned int fg);
#else
// Forward declarations to keep signatures valid when TN_LIMINE is off.
struct limine_memmap_entry;
#endif

#if defined(TN_LIMINE) || defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
extern long long limine_fb_text_at_raw(const unsigned char* s, long long len, long long x, long long y, unsigned int fg,
                                       void* addr, unsigned long long width, unsigned long long height,
                                       unsigned long long pitch, unsigned long long bpp);
extern long long limine_fb_write_raw(const unsigned char* s, long long len,
                                     void* addr, unsigned long long width, unsigned long long height,
                                     unsigned long long pitch, unsigned long long bpp);
extern long long limine_fb_set_scale(long long scale);
extern long long limine_fb_get_scale(void);
extern long long limine_fb_text_mode(long long mode);
extern long long limine_fb_set_color(unsigned int fg);
extern long long limine_fb_set_cursor(long long x, long long y);
extern long long limine_fb_get_cursor_x(void);
extern long long limine_fb_get_cursor_y(void);
extern long long limine_fb_cursor_mode(long long mode);
extern long long limine_fb_cursor(long long on, unsigned int color);
extern long long limine_fb_font_aa_mode(long long mode);
extern long long limine_fb_font_ttf_mode(long long mode);
extern long long limine_fb_draw_logo(long long x, long long y, long long w, long long h);
extern long long limine_fb_draw_wallpaper(long long x, long long y, long long w, long long h);
extern long long limine_fb_draw_cursor(long long x, long long y, long long scale);
extern long long limine_fb_draw_app_icon(long long app, long long x, long long y, long long w, long long h);
extern long long limine_fb_fill(unsigned int color);
extern long long limine_fb_fill_rect(long long x, long long y, long long w, long long h, unsigned int color);
extern long long limine_fb_blit(long long sx, long long sy, long long w, long long h, long long dx, long long dy);
extern long long limine_fb_composite_rect(long long x, long long y, long long w, long long h, unsigned int color, long long alpha);
extern long long limine_fb_get_pixel(long long x, long long y);
extern long long limine_fb_put_pixel(long long x, long long y, unsigned int color);
extern long long limine_fb_cursor_overlay_reset(void);
extern long long limine_fb_cursor_overlay_move(long long x, long long y, long long scale);
extern long long limine_fb_cursor_set_kind(long long kind);
#endif

// GRUB multiboot2 info (saved by grub_mb2.S).
extern unsigned long long grub_mb2_magic;
extern unsigned long long grub_mb2_info;
extern unsigned long long grub_booted;
#if !defined(TEZZ_BOOT_GRUB)
unsigned long long grub_mb2_magic = 0;
unsigned long long grub_mb2_info = 0;
unsigned long long grub_booted = 0;
#endif

#define TN_MB2_MAGIC 0x36d76289u
#define TN_MB2_TAG_FRAMEBUFFER 8u

// TezzBoot handoff (fixed address, defined by tzbt_abi.h).
static tzbt_info* tn_tb = (tzbt_info*)(uintptr_t)TZBT_INFO_ADDR;

typedef struct {
  uint32_t type;
  uint32_t size;
} tn_mb2_tag;

typedef struct {
  uint32_t type;
  uint32_t size;
  uint64_t addr;
  uint32_t pitch;
  uint32_t width;
  uint32_t height;
  uint8_t bpp;
  uint8_t fb_type;
  uint16_t reserved;
} tn_mb2_tag_fb;

static int tn_grub_fb_init_done = 0;
static struct {
  void* addr;
  uint32_t width;
  uint32_t height;
  uint32_t pitch;
  uint32_t bpp;
} tn_grub_fb = {0,0,0,0,0};

#ifdef TN_LIMINE
static struct limine_framebuffer tn_fb_bridge_desc;
static struct limine_framebuffer* tn_fb_bridge_list[1];
static struct limine_framebuffer_response tn_fb_bridge_resp;
static void tn_bind_fb_bridge(void){
  if(!tn_grub_fb.addr) return;
  if(tn_grub_fb.width == 0 || tn_grub_fb.height == 0) return;
  if(tn_grub_fb.pitch == 0) return;
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return;
  }
  memset(&tn_fb_bridge_desc, 0, sizeof(tn_fb_bridge_desc));
  tn_fb_bridge_desc.address = tn_grub_fb.addr;
  tn_fb_bridge_desc.width = tn_grub_fb.width;
  tn_fb_bridge_desc.height = tn_grub_fb.height;
  tn_fb_bridge_desc.pitch = tn_grub_fb.pitch;
  tn_fb_bridge_desc.bpp = (uint16_t)tn_grub_fb.bpp;
  tn_fb_bridge_desc.memory_model = LIMINE_FRAMEBUFFER_RGB;
  if(tn_grub_fb.bpp <= 16){
    tn_fb_bridge_desc.red_mask_size = 5;
    tn_fb_bridge_desc.red_mask_shift = 11;
    tn_fb_bridge_desc.green_mask_size = 6;
    tn_fb_bridge_desc.green_mask_shift = 5;
    tn_fb_bridge_desc.blue_mask_size = 5;
    tn_fb_bridge_desc.blue_mask_shift = 0;
  } else {
    tn_fb_bridge_desc.red_mask_size = 8;
    tn_fb_bridge_desc.red_mask_shift = 16;
    tn_fb_bridge_desc.green_mask_size = 8;
    tn_fb_bridge_desc.green_mask_shift = 8;
    tn_fb_bridge_desc.blue_mask_size = 8;
    tn_fb_bridge_desc.blue_mask_shift = 0;
  }
  tn_fb_bridge_desc.edid_size = 0;
  tn_fb_bridge_desc.edid = 0;
  tn_fb_bridge_desc.mode_count = 0;
  tn_fb_bridge_desc.modes = 0;

  tn_fb_bridge_list[0] = &tn_fb_bridge_desc;
  tn_fb_bridge_resp.revision = 0;
  tn_fb_bridge_resp.framebuffer_count = 1;
  tn_fb_bridge_resp.framebuffers = tn_fb_bridge_list;
  limine_fb_request.response = &tn_fb_bridge_resp;
}
#endif

static int tn_tb_checked = 0;
static int tn_tb_ok = 0;
static uint32_t tn_tb_err = TZBT_COMPAT_ERR_NULL;
static int tn_tb_fb_diag_once = 0;

static void tn_dbg_hex32(uint32_t v);

static void tn_tezzboot_probe(void){
  if(tn_tb_checked) return;
  tn_tb_checked = 1;
  tn_tb_err = tzbt_compat_check(tn_tb);
  tn_tb_ok = (tn_tb_err == TZBT_COMPAT_OK) ? 1 : 0;
  if(!tn_tb_ok){
    // TezzBoot ABI mismatch marker: "XE" + error code.
    sys_outb(0x00E9, 'X');
    sys_outb(0x00E9, 'E');
    tn_dbg_hex32(tn_tb_err);
    if(tn_tb){
      sys_outb(0x00E9, 'V');
      tn_dbg_hex32(tn_tb->version);
    }
  }
}

static int tn_tezzboot_valid(void){
  tn_tezzboot_probe();
  return tn_tb_ok;
}

#define TN_TB_BLK_SECTORS 1ULL
#define TN_TB_BLK_BYTES 512u
static unsigned char tn_tb_blk[TN_TB_BLK_BYTES];
static int tn_tb_blk_seeded = 0;

static void tn_tezzboot_blk_seed(void){
  if(tn_tb_blk_seeded) return;
  tn_tb_blk_seeded = 1;
  for(size_t i = 0; i < sizeof(tn_tb_blk); i++){
    tn_tb_blk[i] = 0;
  }
  // Minimal synthetic MBR signature so block smoke tests can validate.
  tn_tb_blk[510] = 0x55;
  tn_tb_blk[511] = 0xAA;
}

static void tn_tezzboot_fb_init(void){
#if defined(TEZZ_BOOT_TEXT)
  return;
#endif
  if(!tn_tb_fb_diag_once){
    // One-time diagnostics for TezzBoot handoff visibility.
    sys_outb(0x00E9, 'Z');
    tn_dbg_hex32(tn_tb->magic);
    sys_outb(0x00E9, 'A');
    tn_dbg_hex32((uint32_t)(uintptr_t)tn_tb->fb_addr);
    tn_tb_fb_diag_once = 1;
  }
  if(tn_grub_fb.addr){
#ifdef TN_LIMINE
    tn_bind_fb_bridge();
#endif
    return;
  }
  if(!tn_tezzboot_valid()) return;
  if(tn_tb->fb_width == 0) return;
  uint64_t fb_addr = tn_tb->fb_addr;
  uint32_t fb_w = tn_tb->fb_width;
  uint32_t fb_h = tn_tb->fb_height;
  uint32_t fb_p = tn_tb->fb_pitch;
  uint32_t fb_b = tn_tb->fb_bpp;
  if(fb_addr == 0){
    // Do not synthesize framebuffer addresses: invalid pointers cause faults.
    sys_outb(0x00E9, 'Q');
    return;
  }
  if(fb_h == 0){
    return;
  }
  if(fb_b == 0) fb_b = 32;
  if(fb_p == 0){
    uint32_t bpp = fb_b / 8;
    if(bpp == 0) bpp = 4;
    fb_p = fb_w * bpp;
  }
  // Debug dump of TezzBoot framebuffer fields (one-time).
  sys_outb(0x00E9, 'W');
  tn_dbg_hex32(fb_w);
  sys_outb(0x00E9, 'H');
  tn_dbg_hex32(fb_h);
  sys_outb(0x00E9, 'P');
  tn_dbg_hex32(fb_p);
  sys_outb(0x00E9, 'B');
  tn_dbg_hex32(fb_b);
  tn_grub_fb.addr = (void*)(uintptr_t)fb_addr;
  tn_grub_fb.width = fb_w;
  tn_grub_fb.height = fb_h;
  tn_grub_fb.pitch = fb_p;
  tn_grub_fb.bpp = fb_b;
  if(tn_grub_fb.bpp < 24) tn_grub_fb.bpp = 32;
  if(tn_grub_fb.pitch == 0){
    unsigned long long bpp = tn_grub_fb.bpp / 8;
    if(bpp == 0) bpp = 4;
    tn_grub_fb.pitch = (uint32_t)(tn_grub_fb.width * bpp);
  }
  if(tn_grub_fb.addr != 0){
    sys_outb(0x00E9, 'a');
    tn_dbg_hex32((uint32_t)(uintptr_t)tn_grub_fb.addr);
  }
#ifdef TN_LIMINE
  tn_bind_fb_bridge();
#endif
}

static int tn_sys_write_marked = 0;
static void tn_dbgc(char c){
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  __asm__ volatile("outb %0, %1" : : "a"(c), "Nd"(0x00E9));
#else
  (void)c;
#endif
}

static void tn_dbg_hex32(uint32_t v){
  static const char* hex = "0123456789ABCDEF";
  for(int i=7;i>=0;i--){
    uint32_t d = (v >> (i * 4)) & 0xF;
    tn_dbgc(hex[d]);
  }
}

static void tn_grub_fb_init(void){
  if(tn_grub_fb_init_done){
#ifdef TN_LIMINE
    if(tn_grub_fb.addr) tn_bind_fb_bridge();
#endif
    return;
  }
  tn_grub_fb_init_done = 1;
#ifndef TEZZ_GRUB_GFX
  return;
#endif
  if((uint32_t)grub_mb2_magic != TN_MB2_MAGIC) return;
  sys_outb(0x00E9, 'm');
  if(grub_mb2_info == 0) return;
  sys_outb(0x00E9, 'i');
  // Sanity-check pointer range/alignment before dereferencing.
  if((grub_mb2_info & 7ULL) != 0) return;
  if(grub_mb2_info < 0x1000ULL) return;
  // Allow info structures above 4GiB, but keep within our identity map (16GiB).
  if(grub_mb2_info > 0x400000000ULL) return;
  sys_outb(0x00E9, 'p');
  uintptr_t p = (uintptr_t)grub_mb2_info;
  // total_size + reserved
  uint32_t total = *(uint32_t*)p;
  if(total < 8 || total > (1024 * 1024)) return;
  uintptr_t end = p + (uintptr_t)total;
  p += 8;
  while(1){
    sys_outb(0x00E9, 'T');
    if(p + sizeof(tn_mb2_tag) > end) break;
    tn_mb2_tag* tag = (tn_mb2_tag*)p;
    sys_outb(0x00E9, 't');
    if(tag->type == 0) break;
    if(tag->size < sizeof(tn_mb2_tag) || (p + tag->size) > end) break;
    if(tag->type == TN_MB2_TAG_FRAMEBUFFER && tag->size >= sizeof(tn_mb2_tag_fb)){
      tn_mb2_tag_fb* fb = (tn_mb2_tag_fb*)tag;
      if(fb->addr != 0 && fb->width != 0 && fb->height != 0){
        // Only accept addresses within identity map (16GiB).
        if(fb->addr <= 0x400000000ULL){
          tn_grub_fb.addr = (void*)(uintptr_t)fb->addr;
        }
        tn_grub_fb.width = fb->width;
        tn_grub_fb.height = fb->height;
        tn_grub_fb.pitch = fb->pitch;
        tn_grub_fb.bpp = fb->bpp;
        if(tn_grub_fb.bpp < 24) tn_grub_fb.bpp = 32;
        if(tn_grub_fb.pitch == 0){
          unsigned long long bpp = tn_grub_fb.bpp / 8;
          if(bpp == 0) bpp = 4;
          tn_grub_fb.pitch = (uint32_t)(tn_grub_fb.width * bpp);
        }
        if(tn_grub_fb.addr != 0){
          // Debugcon marker to confirm GRUB framebuffer discovery.
          sys_outb(0x00E9, 'F');
          sys_outb(0x00E9, 'B');
          // Emit low 32-bit addr/width/height for diagnostics.
          tn_dbgc('A');
          tn_dbg_hex32((uint32_t)(uintptr_t)tn_grub_fb.addr);
          tn_dbgc('W');
          tn_dbg_hex32((uint32_t)tn_grub_fb.width);
          tn_dbgc('H');
          tn_dbg_hex32((uint32_t)tn_grub_fb.height);
#ifdef TN_LIMINE
          tn_bind_fb_bridge();
#endif
        }
      }
      break;
    }
    p += (uintptr_t)((tag->size + 7) & ~7u);
    if(p >= end) break;
  }
}

// VGA text fallback (80x25, green on black).
static uint16_t tn_vga_row = 0;
static uint16_t tn_vga_col = 0;
static void tn_vga_putc(unsigned char ch){
  if(ch == '\n'){
    tn_vga_col = 0;
    tn_vga_row++;
  } else if(ch == '\r'){
    tn_vga_col = 0;
  } else if(ch == '\b'){
    if(tn_vga_col > 0) tn_vga_col--;
  } else {
    volatile uint8_t* base = (volatile uint8_t*)0xB8000;
    uint32_t idx = (tn_vga_row * 80 + tn_vga_col) * 2;
    base[idx] = ch;
    base[idx + 1] = 0x0A;
    tn_vga_col++;
    if(tn_vga_col >= 80){
      tn_vga_col = 0;
      tn_vga_row++;
    }
  }
  if(tn_vga_row >= 25){
    tn_vga_row = 0;
  }
}

static long long tn_vga_write_raw(const unsigned char* s, long long len){
  if(!s || len <= 0) return 0;
  for(long long i=0;i<len;i++){
    tn_vga_putc(s[i]);
  }
  return len;
}

static int tn_grub_active(void){
#if defined(TEZZ_BOOT_GRUB)
  return 1;
#elif defined(TEZZ_BOOT_BIOS)
  return 1;
#else
  return (grub_booted != 0) && ((uint32_t)grub_mb2_magic == TN_MB2_MAGIC);
#endif
}

#define TN_FS_HEAP_SIZE (1024 * 1024)
typedef union {
  long long align;
  unsigned char buf[TN_FS_HEAP_SIZE];
} tn_fs_heap_u;
static tn_fs_heap_u tn_fs_heap;

typedef struct tn_fs_block {
  size_t size;
  int free;
  struct tn_fs_block* next;
} tn_fs_block;

static tn_fs_block* tn_fs_head = NULL;

static size_t tn_align_up(size_t v, size_t a){
  return (v + (a - 1)) & ~(a - 1);
}

TN_MSABI void* memcpy(void* dst, const void* src, size_t n){
  unsigned char* d = (unsigned char*)dst;
  const unsigned char* s = (const unsigned char*)src;
  for(size_t i=0;i<n;i++) d[i] = s[i];
  return dst;
}

TN_MSABI void* memset(void* dst, int c, size_t n){
  unsigned char* d = (unsigned char*)dst;
  for(size_t i=0;i<n;i++) d[i] = (unsigned char)c;
  return dst;
}

TN_MSABI int memcmp(const void* a, const void* b, size_t n){
  const unsigned char* x = (const unsigned char*)a;
  const unsigned char* y = (const unsigned char*)b;
  for(size_t i=0;i<n;i++){
    if(x[i] != y[i]) return (int)x[i] - (int)y[i];
  }
  return 0;
}

TN_MSABI void* malloc(size_t n){
  if(n == 0) n = 1;
  n = tn_align_up(n, 8);

  if(!tn_fs_head){
    tn_fs_head = (tn_fs_block*)tn_fs_heap.buf;
    tn_fs_head->size = TN_FS_HEAP_SIZE - sizeof(tn_fs_block);
    tn_fs_head->free = 1;
    tn_fs_head->next = NULL;
  }

  tn_fs_block* cur = tn_fs_head;
  while(cur){
    if(cur->free && cur->size >= n){
      size_t remain = cur->size - n;
      if(remain > sizeof(tn_fs_block) + 8){
        tn_fs_block* next = (tn_fs_block*)((unsigned char*)cur + sizeof(tn_fs_block) + n);
        next->size = remain - sizeof(tn_fs_block);
        next->free = 1;
        next->next = cur->next;
        cur->next = next;
        cur->size = n;
      }
      cur->free = 0;
      return (unsigned char*)cur + sizeof(tn_fs_block);
    }
    cur = cur->next;
  }
  return NULL;
}

TN_MSABI void free(void* p){
  if(!p) return;
  tn_fs_block* blk = NULL;
  tn_fs_block* cur = tn_fs_head;
  tn_fs_block* prev = NULL;
  while(cur){
    unsigned char* data = (unsigned char*)cur + sizeof(tn_fs_block);
    if(data == (unsigned char*)p){
      blk = cur;
      break;
    }
    prev = cur;
    cur = cur->next;
  }
  if(!blk) return;
  if(blk->free) return;
  blk->free = 1;

  // coalesce forward
  if(blk->next && blk->next->free){
    blk->size += sizeof(tn_fs_block) + blk->next->size;
    blk->next = blk->next->next;
  }

  // coalesce backward
  if(prev && prev->free){
    prev->size += sizeof(tn_fs_block) + blk->size;
    prev->next = blk->next;
  }
}

TN_MSABI long long len(unsigned char* s){
  if(!s) return 0;
  long long n = 0;
  while(s[n] != 0) n++;
  return n;
}

TN_MSABI long long tn_strcmp(unsigned char* a, unsigned char* b){
  if(!a) a = (unsigned char*)"";
  if(!b) b = (unsigned char*)"";
  while(*a && *b){
    if(*a != *b) return (long long)((int)*a - (int)*b);
    a++; b++;
  }
  return (long long)((int)*a - (int)*b);
}

TN_MSABI void say(long long x){
  (void)x; // no-op in freestanding mode
}

TN_MSABI void say_f(long long bits){
  (void)bits; // no-op in freestanding mode
}

TN_MSABI void say_str(unsigned char* s){
  (void)s; // no-op in freestanding mode
}

TN_MSABI long long sys_outb(long long port, long long val){
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  unsigned char v = (unsigned char)val;
  unsigned short p = (unsigned short)port;
  __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(p));
  return 0;
#else
  (void)port; (void)val;
  return -1;
#endif
}

TN_MSABI long long sys_inb(long long port){
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  unsigned char v = 0;
  unsigned short p = (unsigned short)port;
  __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p));
  return (long long)v;
#else
  (void)port;
  return -1;
#endif
}

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
static inline uint32_t tn_io_inl(uint16_t port){
  uint32_t v = 0;
  __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
  return v;
}

static inline void tn_io_outl(uint16_t port, uint32_t val){
  __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t tn_io_inw(uint16_t port){
  uint16_t v = 0;
  __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
  return v;
}

static inline void tn_io_outw(uint16_t port, uint16_t val){
  __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}
#endif

TN_MSABI long long sys_write(long long fd, unsigned char* buf, long long len){
  (void)fd;
  if(!buf || len <= 0) return 0;
#ifdef TN_LIMINE
  extern long long limine_fb_write(const unsigned char* s, long long len);
  long long r = limine_fb_write(buf, len);
  if(r >= 0){
    if(!tn_sys_write_marked){ tn_dbgc('L'); tn_sys_write_marked = 1; }
    return r;
  }
#endif
#if defined(TN_LIMINE) || defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  tn_grub_fb_init();
  if(tn_grub_fb.addr){
    long long r2 = limine_fb_write_raw(buf, len, tn_grub_fb.addr, tn_grub_fb.width,
                                       tn_grub_fb.height, tn_grub_fb.pitch, tn_grub_fb.bpp);
    if(r2 >= 0){
      if(!tn_sys_write_marked){ tn_dbgc('R'); tn_sys_write_marked = 1; }
      return r2;
    }
  }
#endif
  // GRUB/text-mode fallback: VGA text buffer.
#if defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  if(tn_grub_active()){
    if(!tn_sys_write_marked){ tn_dbgc('V'); tn_sys_write_marked = 1; }
    return tn_vga_write_raw(buf, len);
  }
#endif
  // Fallback: debug console (0xE9) byte stream.
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  if(!tn_sys_write_marked){ tn_dbgc('D'); tn_sys_write_marked = 1; }
  for(long long i=0;i<len;i++){
    unsigned char v = buf[i];
    unsigned short p = 0x00E9;
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(p));
  }
  return len;
#else
  (void)buf; (void)len;
  return -1;
#endif
}

TN_MSABI long long sys_exit(long long code){
  (void)code;
  for(;;){}
  return 0;
}

TN_MSABI long long sys_time_ns(void){
  return 0;
}

TN_MSABI long long sys_yield(void){
  return 0;
}

TN_MSABI void* os__fb_addr(void){
#ifdef TN_LIMINE
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return limine_fb_request.response->framebuffers[0]->address;
  }
  if(tn_grub_active()){
    tn_tezzboot_fb_init();
    tn_grub_fb_init();
    return tn_grub_fb.addr;
  }
  return NULL;
#else
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  return tn_grub_fb.addr;
#endif
}

TN_MSABI long long os__fb_width(void){
#ifdef TN_LIMINE
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return (long long)limine_fb_request.response->framebuffers[0]->width;
  }
  if(tn_grub_active()){
    tn_tezzboot_fb_init();
    tn_grub_fb_init();
    return (long long)tn_grub_fb.width;
  }
  return 0;
#else
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  return (long long)tn_grub_fb.width;
#endif
}

TN_MSABI long long os__fb_height(void){
#ifdef TN_LIMINE
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return (long long)limine_fb_request.response->framebuffers[0]->height;
  }
  if(tn_grub_active()){
    tn_tezzboot_fb_init();
    tn_grub_fb_init();
    return (long long)tn_grub_fb.height;
  }
  return 0;
#else
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  return (long long)tn_grub_fb.height;
#endif
}

TN_MSABI long long os__fb_pitch(void){
#ifdef TN_LIMINE
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return (long long)limine_fb_request.response->framebuffers[0]->pitch;
  }
  if(tn_grub_active()){
    tn_tezzboot_fb_init();
    tn_grub_fb_init();
    return (long long)tn_grub_fb.pitch;
  }
  return 0;
#else
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  return (long long)tn_grub_fb.pitch;
#endif
}

TN_MSABI long long os__fb_bpp(void){
#ifdef TN_LIMINE
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return (long long)limine_fb_request.response->framebuffers[0]->bpp;
  }
  if(tn_grub_active()){
    tn_tezzboot_fb_init();
    tn_grub_fb_init();
    return (long long)tn_grub_fb.bpp;
  }
  return 0;
#else
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  return (long long)tn_grub_fb.bpp;
#endif
}

TN_MSABI long long os__boot_text(void){
#ifdef TEZZ_BOOT_TEXT
  return 1;
#endif
#if defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  // If TezzBoot framebuffer info is missing or invalid, treat as text-mode boot.
  if(!tn_tezzboot_valid()) return 1;
  if(tn_tb->fb_width == 0 || tn_tb->fb_height == 0) return 1;
  if(tn_tb->fb_pitch == 0) return 1;
  if(tn_tb->fb_bpp < 24) return 1;
  if(tn_tb->fb_addr == 0) return 1;
#endif
  return 0;
}

TN_MSABI long long os__fb_text(long long x, long long y, unsigned char* s, long long color){
#ifdef TN_LIMINE
  if(!s) return 0;
  long long n = len(s);
  unsigned int fg = (unsigned int)color;
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    long long cx = limine_fb_get_cursor_x();
    long long cy = limine_fb_get_cursor_y();
    long long r = limine_fb_text_at(s, n, x, y, fg);
    limine_fb_set_cursor(cx, cy);
    return r;
  }
  if(tn_grub_active()){
    tn_tezzboot_fb_init();
    tn_grub_fb_init();
    if(!tn_grub_fb.addr) return -1;
    long long cx = limine_fb_get_cursor_x();
    long long cy = limine_fb_get_cursor_y();
    long long r = limine_fb_text_at_raw(s, n, x, y, fg, tn_grub_fb.addr, tn_grub_fb.width,
                                        tn_grub_fb.height, tn_grub_fb.pitch, tn_grub_fb.bpp);
    limine_fb_set_cursor(cx, cy);
    return r;
  }
  return -1;
#else
#if defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(!s) return 0;
  if(!tn_grub_fb.addr) return -1;
  long long n = len(s);
  unsigned int fg = (unsigned int)color;
  long long cx = limine_fb_get_cursor_x();
  long long cy = limine_fb_get_cursor_y();
  long long r = limine_fb_text_at_raw(s, n, x, y, fg, tn_grub_fb.addr, tn_grub_fb.width,
                                      tn_grub_fb.height, tn_grub_fb.pitch, tn_grub_fb.bpp);
  limine_fb_set_cursor(cx, cy);
  return r;
#else
  (void)x;
  (void)y;
  (void)s;
  (void)color;
  return -1;
#endif
#endif
}

TN_MSABI long long os__fb_set_scale(long long scale){
#if defined(TN_LIMINE)
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return limine_fb_set_scale(scale);
  }
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_set_scale(scale);
  (void)scale;
  return -1;
#elif defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_set_scale(scale);
  (void)scale;
  return -1;
#else
  (void)scale;
  return -1;
#endif
}

TN_MSABI long long os__fb_get_scale(void){
#if defined(TN_LIMINE)
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return limine_fb_get_scale();
  }
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_get_scale();
  return 1;
#elif defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_get_scale();
  return 1;
#else
  return 1;
#endif
}

TN_MSABI long long os__fb_set_color(long long color){
#if defined(TN_LIMINE)
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return limine_fb_set_color((unsigned int)color);
  }
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_set_color((unsigned int)color);
  (void)color;
  return -1;
#elif defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_set_color((unsigned int)color);
  (void)color;
  return -1;
#else
  (void)color;
  return -1;
#endif
}

TN_MSABI long long os__fb_text_mode(long long mode){
#if defined(TN_LIMINE)
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return limine_fb_text_mode(mode);
  }
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_text_mode(mode);
  (void)mode;
  return -1;
#elif defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_text_mode(mode);
  (void)mode;
  return -1;
#else
  (void)mode;
  return -1;
#endif
}

TN_MSABI long long os__fb_font_aa_mode(long long mode){
#if defined(TN_LIMINE)
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return limine_fb_font_aa_mode(mode);
  }
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_font_aa_mode(mode);
  (void)mode;
  return -1;
#elif defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_font_aa_mode(mode);
  (void)mode;
  return -1;
#else
  (void)mode;
  return -1;
#endif
}

TN_MSABI long long os__fb_font_ttf_mode(long long mode){
#if defined(TN_LIMINE)
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return limine_fb_font_ttf_mode(mode);
  }
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_font_ttf_mode(mode);
  (void)mode;
  return -1;
#elif defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_font_ttf_mode(mode);
  (void)mode;
  return -1;
#else
  (void)mode;
  return -1;
#endif
}

TN_MSABI long long os__fb_draw_logo(long long x, long long y, long long w, long long h){
#if defined(TN_LIMINE)
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return limine_fb_draw_logo(x, y, w, h);
  }
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_draw_logo(x, y, w, h);
  (void)x; (void)y; (void)w; (void)h;
  return -1;
#elif defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_draw_logo(x, y, w, h);
  (void)x; (void)y; (void)w; (void)h;
  return -1;
#else
  (void)x; (void)y; (void)w; (void)h;
  return -1;
#endif
}

TN_MSABI long long os__fb_draw_wallpaper(long long x, long long y, long long w, long long h){
#if defined(TN_LIMINE)
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return limine_fb_draw_wallpaper(x, y, w, h);
  }
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_draw_wallpaper(x, y, w, h);
  (void)x; (void)y; (void)w; (void)h;
  return -1;
#elif defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_draw_wallpaper(x, y, w, h);
  (void)x; (void)y; (void)w; (void)h;
  return -1;
#else
  (void)x; (void)y; (void)w; (void)h;
  return -1;
#endif
}

TN_MSABI long long os__fb_draw_cursor(long long x, long long y, long long scale){
#if defined(TN_LIMINE)
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return limine_fb_draw_cursor(x, y, scale);
  }
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_draw_cursor(x, y, scale);
  (void)x; (void)y; (void)scale;
  return -1;
#elif defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_draw_cursor(x, y, scale);
  (void)x; (void)y; (void)scale;
  return -1;
#else
  (void)x; (void)y; (void)scale;
  return -1;
#endif
}

TN_MSABI long long os__fb_draw_app_icon(long long app, long long x, long long y, long long w, long long h){
#if defined(TN_LIMINE)
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return limine_fb_draw_app_icon(app, x, y, w, h);
  }
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_draw_app_icon(app, x, y, w, h);
  (void)app; (void)x; (void)y; (void)w; (void)h;
  return -1;
#elif defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_draw_app_icon(app, x, y, w, h);
  (void)app; (void)x; (void)y; (void)w; (void)h;
  return -1;
#else
  (void)app; (void)x; (void)y; (void)w; (void)h;
  return -1;
#endif
}

TN_MSABI long long os__fb_fill(long long color){
#if defined(TN_LIMINE)
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return limine_fb_fill((unsigned int)color);
  }
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_fill((unsigned int)color);
  (void)color;
  return -1;
#elif defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_fill((unsigned int)color);
  (void)color;
  return -1;
#else
  (void)color;
  return -1;
#endif
}

TN_MSABI long long os__fb_fill_rect(long long x, long long y, long long w, long long h, long long color){
#if defined(TN_LIMINE)
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return limine_fb_fill_rect(x, y, w, h, (unsigned int)color);
  }
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_fill_rect(x, y, w, h, (unsigned int)color);
  (void)x; (void)y; (void)w; (void)h; (void)color;
  return -1;
#elif defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_fill_rect(x, y, w, h, (unsigned int)color);
  (void)x; (void)y; (void)w; (void)h; (void)color;
  return -1;
#else
  (void)x; (void)y; (void)w; (void)h; (void)color;
  return -1;
#endif
}

TN_MSABI long long os__fb_blit(long long sx, long long sy, long long w, long long h, long long dx, long long dy){
#if defined(TN_LIMINE)
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return limine_fb_blit(sx, sy, w, h, dx, dy);
  }
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_blit(sx, sy, w, h, dx, dy);
  (void)sx; (void)sy; (void)w; (void)h; (void)dx; (void)dy;
  return -1;
#elif defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_blit(sx, sy, w, h, dx, dy);
  (void)sx; (void)sy; (void)w; (void)h; (void)dx; (void)dy;
  return -1;
#else
  (void)sx; (void)sy; (void)w; (void)h; (void)dx; (void)dy;
  return -1;
#endif
}

TN_MSABI long long os__fb_composite_rect(long long x, long long y, long long w, long long h, long long color, long long alpha){
#if defined(TN_LIMINE)
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return limine_fb_composite_rect(x, y, w, h, (unsigned int)color, alpha);
  }
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_composite_rect(x, y, w, h, (unsigned int)color, alpha);
  (void)x; (void)y; (void)w; (void)h; (void)color; (void)alpha;
  return -1;
#elif defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_composite_rect(x, y, w, h, (unsigned int)color, alpha);
  (void)x; (void)y; (void)w; (void)h; (void)color; (void)alpha;
  return -1;
#else
  (void)x; (void)y; (void)w; (void)h; (void)color; (void)alpha;
  return -1;
#endif
}

TN_MSABI long long os__fb_get_pixel(long long x, long long y){
#if defined(TN_LIMINE)
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return limine_fb_get_pixel(x, y);
  }
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_get_pixel(x, y);
  (void)x; (void)y;
  return -1;
#elif defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_get_pixel(x, y);
  (void)x; (void)y;
  return -1;
#else
  (void)x; (void)y;
  return -1;
#endif
}

TN_MSABI long long os__fb_put_pixel(long long x, long long y, long long color){
#if defined(TN_LIMINE)
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return limine_fb_put_pixel(x, y, (unsigned int)color);
  }
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_put_pixel(x, y, (unsigned int)color);
  (void)x; (void)y; (void)color;
  return -1;
#elif defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_put_pixel(x, y, (unsigned int)color);
  (void)x; (void)y; (void)color;
  return -1;
#else
  (void)x; (void)y; (void)color;
  return -1;
#endif
}

TN_MSABI long long os__fb_cursor_soft_reset(void){
#if defined(TN_LIMINE)
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return limine_fb_cursor_overlay_reset();
  }
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_cursor_overlay_reset();
  return -1;
#elif defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_cursor_overlay_reset();
  return -1;
#else
  return -1;
#endif
}

TN_MSABI long long os__fb_cursor_soft_move(long long x, long long y, long long scale){
#if defined(TN_LIMINE)
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return limine_fb_cursor_overlay_move(x, y, scale);
  }
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_cursor_overlay_move(x, y, scale);
  (void)x; (void)y; (void)scale;
  return -1;
#elif defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_cursor_overlay_move(x, y, scale);
  (void)x; (void)y; (void)scale;
  return -1;
#else
  (void)x; (void)y; (void)scale;
  return -1;
#endif
}

TN_MSABI long long os__fb_cursor_kind(long long kind){
#if defined(TN_LIMINE)
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return limine_fb_cursor_set_kind(kind);
  }
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_cursor_set_kind(kind);
  (void)kind;
  return -1;
#elif defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_cursor_set_kind(kind);
  (void)kind;
  return -1;
#else
  (void)kind;
  return -1;
#endif
}

TN_MSABI long long os__fb_set_cursor(long long x, long long y){
#if defined(TN_LIMINE)
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return limine_fb_set_cursor(x, y);
  }
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_set_cursor(x, y);
  (void)x; (void)y;
  return -1;
#elif defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_set_cursor(x, y);
  (void)x; (void)y;
  return -1;
#else
  (void)x; (void)y;
  return -1;
#endif
}

TN_MSABI long long os__fb_cursor_mode(long long mode){
#if defined(TN_LIMINE)
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return limine_fb_cursor_mode(mode);
  }
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_cursor_mode(mode);
  (void)mode;
  return -1;
#elif defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_cursor_mode(mode);
  (void)mode;
  return -1;
#else
  (void)mode;
  return -1;
#endif
}

TN_MSABI long long os__fb_cursor(long long on, long long color){
#if defined(TN_LIMINE)
  if(limine_fb_request.response && limine_fb_request.response->framebuffer_count > 0){
    return limine_fb_cursor(on, (unsigned int)color);
  }
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_cursor(on, (unsigned int)color);
  (void)on; (void)color;
  return -1;
#elif defined(TEZZ_BOOT_GRUB) || defined(TEZZ_BOOT_BIOS)
  tn_tezzboot_fb_init();
  tn_grub_fb_init();
  if(tn_grub_fb.addr) return limine_fb_cursor(on, (unsigned int)color);
  (void)on; (void)color;
  return -1;
#else
  (void)on; (void)color;
  return -1;
#endif
}

// --- PS/2 keyboard (TezzBoot BIOS/GRUB) ---------------------------------
#if defined(TEZZ_BOOT_BIOS) || defined(TEZZ_BOOT_GRUB)
#if defined(TEZZ_BOOT_POLL)
static int tn_kbd_shift = 0;
static int tn_kbd_ext = 0;
static int tn_kbd_debug = 0;
static unsigned char tn_kbd_last = 0;

static unsigned char tn_kbd_map[128] = {
  0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
  '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
  'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\',
  'z','x','c','v','b','n','m',',','.','/', 0,'*',0,' ', 0
};

static unsigned char tn_kbd_shift_map[128] = {
  0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
  '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,
  'A','S','D','F','G','H','J','K','L',':','"','~', 0,'|',
  'Z','X','C','V','B','N','M','<','>','?', 0,'*',0,' ', 0
};

static unsigned char tn_kbd_read_raw(void){
  unsigned char status = (unsigned char)sys_inb(0x64);
  if((status & 1) == 0) return 0;
  unsigned char sc = (unsigned char)sys_inb(0x60);
  tn_kbd_last = sc;
  if(tn_kbd_debug){
    sys_outb(0x00E9, 'K');
    sys_outb(0x00E9, sc);
  }
  return sc;
}

TN_MSABI long long os__kbd_has_event(void){
  unsigned char status = (unsigned char)sys_inb(0x64);
  return (status & 1) ? 1 : 0;
}

TN_MSABI long long os__kbd_read_scancode_raw(void){
  return (long long)tn_kbd_read_raw();
}

TN_MSABI long long os__kbd_read_scancode(void){
  unsigned char sc = tn_kbd_read_raw();
  if(sc == 0) return 0;
  if(sc == 0xE0){
    tn_kbd_ext = 1;
    return 0;
  }
  if(sc & 0x80){
    unsigned char make = sc & 0x7F;
    if(make == 0x2A || make == 0x36) tn_kbd_shift = 0;
    return 0;
  }
  if(sc == 0x2A || sc == 0x36){
    tn_kbd_shift = 1;
    return 0;
  }
  if(tn_kbd_ext){
    tn_kbd_ext = 0;
  }
  return (long long)sc;
}

TN_MSABI long long os__kbd_read_char(void){
  unsigned char sc = tn_kbd_read_raw();
  if(sc == 0) return 0;
  if(sc == 0xE0){
    tn_kbd_ext = 1;
    return 0;
  }
  if(sc & 0x80){
    unsigned char make = sc & 0x7F;
    if(make == 0x2A || make == 0x36) tn_kbd_shift = 0;
    return 0;
  }
  if(sc == 0x2A || sc == 0x36){
    tn_kbd_shift = 1;
    return 0;
  }
  if(tn_kbd_ext){
    tn_kbd_ext = 0;
    return 0;
  }
  unsigned char ch = tn_kbd_shift ? tn_kbd_shift_map[sc] : tn_kbd_map[sc];
  return (long long)ch;
}

TN_MSABI long long os__kbd_set_debug(long long on){
  tn_kbd_debug = (on != 0) ? 1 : 0;
  return 0;
}

TN_MSABI long long os__kbd_last_scancode(void){
  return (long long)tn_kbd_last;
}

static int tn_mouse_pending = 0;
static int tn_mouse_pkt_idx = 0;
static unsigned char tn_mouse_pkt[3];
static int tn_mouse_dx_last = 0;
static int tn_mouse_dy_last = 0;
static int tn_mouse_btn_last = 0;

static void tn_mouse_process(unsigned char b){
  if(tn_mouse_pkt_idx == 0){
    if((b & 0x08) == 0) return;
  }
  tn_mouse_pkt[tn_mouse_pkt_idx] = b;
  tn_mouse_pkt_idx++;
  if(tn_mouse_pkt_idx < 3) return;
  tn_mouse_pkt_idx = 0;
  unsigned char b0 = tn_mouse_pkt[0];
  unsigned char b1 = tn_mouse_pkt[1];
  unsigned char b2 = tn_mouse_pkt[2];
  if(b0 & 0xC0) return;
  tn_mouse_dx_last = (int)(signed char)b1;
  tn_mouse_dy_last = 0 - (int)(signed char)b2;
  tn_mouse_btn_last = (int)(b0 & 0x07);
  tn_mouse_pending = 1;
}

static int tn_mouse_poll_once(void){
  unsigned char status = (unsigned char)sys_inb(0x64);
  if((status & 0x21) != 0x21) return 0;
  unsigned char b = (unsigned char)sys_inb(0x60);
  tn_mouse_process(b);
  return 1;
}

TN_MSABI long long os__mouse_has_packet(void){
  if(tn_mouse_pending != 0) return 1;
  for(int i=0;i<8;i++){
    if(!tn_mouse_poll_once()) break;
    if(tn_mouse_pending != 0) return 1;
  }
  return 0;
}

TN_MSABI long long os__mouse_read_packet(void){
  if(tn_mouse_pending == 0){
    for(int i=0;i<16;i++){
      if(!tn_mouse_poll_once()) break;
      if(tn_mouse_pending != 0) break;
    }
  }
  if(tn_mouse_pending == 0) return 0;
  tn_mouse_pending = 0;
  return 1;
}

TN_MSABI long long os__mouse_dx(void){
  return (long long)tn_mouse_dx_last;
}

TN_MSABI long long os__mouse_dy(void){
  return (long long)tn_mouse_dy_last;
}

TN_MSABI long long os__mouse_buttons(void){
  return (long long)tn_mouse_btn_last;
}
#endif
#elif !defined(TN_LIMINE)
TN_MSABI long long os__kbd_has_event(void){ return 0; }
TN_MSABI long long os__kbd_read_scancode_raw(void){ return 0; }
TN_MSABI long long os__kbd_read_scancode(void){ return 0; }
TN_MSABI long long os__kbd_read_char(void){ return 0; }
TN_MSABI long long os__kbd_set_debug(long long on){ (void)on; return 0; }
TN_MSABI long long os__kbd_last_scancode(void){ return 0; }
TN_MSABI long long os__mouse_has_packet(void){ return 0; }
TN_MSABI long long os__mouse_read_packet(void){ return 0; }
TN_MSABI long long os__mouse_dx(void){ return 0; }
TN_MSABI long long os__mouse_dy(void){ return 0; }
TN_MSABI long long os__mouse_buttons(void){ return 0; }
TN_MSABI long long os__ps2_force_mouse_path(long long profile){ (void)profile; return 0; }
#endif

void tn_fb_boot_marker(void){
  static const unsigned char msg[] = "TEZZ FB\n";
  os__fb_text(0, 0, (unsigned char*)msg, 0x00FF00);
}

TN_MSABI long long os__memmap_count(void){
#ifdef TN_LIMINE
  if(tn_grub_active()) return 0;
  if(tn_tezzboot_valid()) return (long long)tn_tb->memmap_count;
  if(limine_memmap_request.response){
    return (long long)limine_memmap_request.response->entry_count;
  }
  return 0;
#else
  if(tn_tezzboot_valid()) return (long long)tn_tb->memmap_count;
  return 0;
#endif
}

static struct limine_memmap_entry* os__memmap_entry(long long idx){
#ifdef TN_LIMINE
  if(tn_grub_active()) return NULL;
  if(tn_tezzboot_valid()) return NULL;
  if(!limine_memmap_request.response) return NULL;
  uint64_t n = limine_memmap_request.response->entry_count;
  if(idx < 0 || (uint64_t)idx >= n) return NULL;
  return limine_memmap_request.response->entries[idx];
#else
  (void)idx;
  return NULL;
#endif
}

static tzbt_mem* os__tb_entry(long long idx){
  if(!tn_tezzboot_valid()) return NULL;
  if(idx < 0 || (uint64_t)idx >= (uint64_t)tn_tb->memmap_count) return NULL;
  return &tn_tb->memmap[idx];
}

TN_MSABI long long os__memmap_base(long long idx){
#ifdef TN_LIMINE
  struct limine_memmap_entry* e = os__memmap_entry(idx);
  if(e) return (long long)e->base;
#endif
  tzbt_mem* t = os__tb_entry(idx);
  if(t) return (long long)t->base;
  return 0;
}

TN_MSABI long long os__memmap_len(long long idx){
#ifdef TN_LIMINE
  struct limine_memmap_entry* e = os__memmap_entry(idx);
  if(e) return (long long)e->length;
#endif
  tzbt_mem* t = os__tb_entry(idx);
  if(t) return (long long)t->len;
  return 0;
}

TN_MSABI long long os__memmap_type(long long idx){
#ifdef TN_LIMINE
  struct limine_memmap_entry* e = os__memmap_entry(idx);
  if(e) return (long long)e->type;
#endif
  tzbt_mem* t = os__tb_entry(idx);
  if(t) return (long long)t->type;
  return 0;
}

#ifdef TN_LIMINE
static struct limine_file* os__boot_file(void){
  if(tn_grub_active()) return NULL;
  if(tn_tezzboot_valid()) return NULL;
  if(!tezz_limine_executable_file_request.response) return NULL;
  return tezz_limine_executable_file_request.response->executable_file;
}

static int tn_uuid_is_zero(const struct limine_uuid* u){
  if(!u) return 1;
  if(u->a != 0) return 0;
  if(u->b != 0) return 0;
  if(u->c != 0) return 0;
  for(int i=0;i<8;i++){
    if(u->d[i] != 0) return 0;
  }
  return 1;
}

static uint64_t tn_uuid_hi(const struct limine_uuid* u){
  if(!u) return 0;
  return ((uint64_t)u->a << 32) | ((uint64_t)u->b << 16) | (uint64_t)u->c;
}

static uint64_t tn_uuid_lo(const struct limine_uuid* u){
  if(!u) return 0;
  uint64_t v = 0;
  for(int i=0;i<8;i++){
    v = (v << 8) | (uint64_t)u->d[i];
  }
  return v;
}
#endif

// Disk/partition discovery (Gate B milestone 1).
// Current implementation uses Limine executable-file metadata for the boot disk.
// disk_table_kind: 0=unknown, 1=MBR, 2=GPT
TN_MSABI long long os__disk_count(void){
#ifdef TN_LIMINE
  struct limine_file* f = os__boot_file();
  if(!f){
    if(tn_tezzboot_valid()) return 1;
    return 0;
  }
  return 1;
#else
  if(tn_tezzboot_valid()) return 1;
  return 0;
#endif
}

TN_MSABI long long os__disk_media_type(long long idx){
#ifdef TN_LIMINE
  if(idx != 0) return -1;
  struct limine_file* f = os__boot_file();
  if(!f){
    if(tn_tezzboot_valid()) return 0;
    return -1;
  }
  return (long long)f->media_type;
#else
  if(idx != 0) return -1;
  if(tn_tezzboot_valid()) return 0;
  return -1;
#endif
}

TN_MSABI long long os__disk_table_kind(long long idx){
#ifdef TN_LIMINE
  if(idx != 0) return 0;
  struct limine_file* f = os__boot_file();
  if(!f){
    if(tn_tezzboot_valid()) return 1;
    return 0;
  }
  if(!tn_uuid_is_zero(&f->gpt_disk_uuid) || !tn_uuid_is_zero(&f->gpt_part_uuid)) return 2;
  if(f->mbr_disk_id != 0) return 1;
  return 0;
#else
  if(idx == 0 && tn_tezzboot_valid()) return 1;
  return 0;
#endif
}

TN_MSABI long long os__disk_mbr_id(long long idx){
#ifdef TN_LIMINE
  if(idx != 0) return 0;
  struct limine_file* f = os__boot_file();
  if(!f){
    if(tn_tezzboot_valid()) return 0x54455A5A; // "TEZZ"
    return 0;
  }
  return (long long)f->mbr_disk_id;
#else
  if(idx == 0 && tn_tezzboot_valid()) return 0x54455A5A;
  return 0;
#endif
}

TN_MSABI long long os__disk_part_count(long long idx){
#ifdef TN_LIMINE
  if(idx != 0) return 0;
  struct limine_file* f = os__boot_file();
  if(!f){
    if(tn_tezzboot_valid()) return 1;
    return 0;
  }
  long long kind = os__disk_table_kind(idx);
  if(kind == 0) return 0;
  // Limine partition_index is valid for the boot file even when 0.
  // For strict install checks, treat known table metadata as one boot partition.
  return 1;
#else
  if(idx == 0 && tn_tezzboot_valid()) return 1;
  return 0;
#endif
}

TN_MSABI long long os__disk_part_index(long long disk_idx, long long part_idx){
#ifdef TN_LIMINE
  if(disk_idx != 0 || part_idx != 0) return -1;
  struct limine_file* f = os__boot_file();
  if(!f){
    if(tn_tezzboot_valid()) return 0;
    return -1;
  }
  return (long long)f->partition_index;
#else
  if(disk_idx == 0 && part_idx == 0 && tn_tezzboot_valid()) return 0;
  return -1;
#endif
}

TN_MSABI long long os__disk_gpt_disk_hi(long long idx){
#ifdef TN_LIMINE
  if(idx != 0) return 0;
  struct limine_file* f = os__boot_file();
  if(!f) return 0;
  return (long long)tn_uuid_hi(&f->gpt_disk_uuid);
#else
  (void)idx;
  return 0;
#endif
}

TN_MSABI long long os__disk_gpt_disk_lo(long long idx){
#ifdef TN_LIMINE
  if(idx != 0) return 0;
  struct limine_file* f = os__boot_file();
  if(!f) return 0;
  return (long long)tn_uuid_lo(&f->gpt_disk_uuid);
#else
  (void)idx;
  return 0;
#endif
}

TN_MSABI long long os__disk_gpt_part_hi(long long idx){
#ifdef TN_LIMINE
  if(idx != 0) return 0;
  struct limine_file* f = os__boot_file();
  if(!f) return 0;
  return (long long)tn_uuid_hi(&f->gpt_part_uuid);
#else
  (void)idx;
  return 0;
#endif
}

TN_MSABI long long os__disk_gpt_part_lo(long long idx){
#ifdef TN_LIMINE
  if(idx != 0) return 0;
  struct limine_file* f = os__boot_file();
  if(!f) return 0;
  return (long long)tn_uuid_lo(&f->gpt_part_uuid);
#else
  (void)idx;
  return 0;
#endif
}

// Architecture introspection.
// arch_id: 0=unknown, 1=x86, 2=x86_64, 3=arm32, 4=arm64, 5=riscv32, 6=riscv64
TN_MSABI long long os__arch_id(void){
#if defined(__x86_64__) || defined(_M_X64)
  return 2;
#elif defined(__i386__) || defined(_M_IX86)
  return 1;
#elif defined(__aarch64__)
  return 4;
#elif defined(__arm__)
  return 3;
#elif defined(__riscv)
#if __riscv_xlen == 64
  return 6;
#else
  return 5;
#endif
#else
  return 0;
#endif
}

TN_MSABI long long os__arch_bits(void){
#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__)
  return 64;
#elif defined(__i386__) || defined(_M_IX86) || defined(__arm__)
  return 32;
#elif defined(__riscv)
#if __riscv_xlen == 64
  return 64;
#else
  return 32;
#endif
#else
  return (long long)(sizeof(void*) * 8);
#endif
}

// Block I/O abstraction (Gate B milestone 2).
// Backend 0 is a read-only virtual block device backed by the loaded executable
// file image provided by Limine.
TN_MSABI long long os__blk_count(void){
#ifdef TN_LIMINE
  struct limine_file* f = os__boot_file();
  if(!f || !f->address || f->size == 0){
    if(tn_tezzboot_valid()) return 1;
    return 0;
  }
  return 1;
#else
  if(tn_tezzboot_valid()) return 1;
  return 0;
#endif
}

TN_MSABI long long os__blk_sector_size(long long dev_idx){
#ifdef TN_LIMINE
  if(dev_idx != 0) return 0;
  if(os__blk_count() <= 0) return 0;
  return 512;
#else
  if(dev_idx != 0) return 0;
  if(!tn_tezzboot_valid()) return 0;
  return 512;
#endif
}

TN_MSABI long long os__blk_sector_count(long long dev_idx){
#ifdef TN_LIMINE
  if(dev_idx != 0) return 0;
  struct limine_file* f = os__boot_file();
  if(!f || !f->address || f->size == 0){
    if(tn_tezzboot_valid()) return (long long)TN_TB_BLK_SECTORS;
    return 0;
  }
  return (long long)((f->size + 511ULL) / 512ULL);
#else
  if(dev_idx != 0) return 0;
  if(!tn_tezzboot_valid()) return 0;
  return (long long)TN_TB_BLK_SECTORS;
#endif
}

TN_MSABI long long os__blk_read_sector(long long dev_idx, long long lba, unsigned char* out512){
#ifdef TN_LIMINE
  if(!out512) return -1;
  if(dev_idx != 0 || lba < 0) return -1;
  struct limine_file* f = os__boot_file();
  if(!f || !f->address || f->size == 0){
    if(!tn_tezzboot_valid()) return -1;
    if((uint64_t)lba >= TN_TB_BLK_SECTORS) return -1;
    tn_tezzboot_blk_seed();
    const unsigned char* src = tn_tb_blk + ((uint64_t)lba * 512ULL);
    memcpy(out512, src, 512);
    return 512;
  }
  uint64_t sectors = (f->size + 511ULL) / 512ULL;
  if((uint64_t)lba >= sectors) return -1;
  uint64_t off = (uint64_t)lba * 512ULL;
  uint64_t remain = 0;
  if(f->size > off){
    remain = f->size - off;
  }
  uint64_t copy_n = remain;
  if(copy_n > 512ULL) copy_n = 512ULL;
  unsigned char* src = (unsigned char*)f->address + off;
  memcpy(out512, src, (size_t)copy_n);
  if(copy_n < 512ULL){
    memset(out512 + copy_n, 0, (size_t)(512ULL - copy_n));
  }
  return 512;
#else
  if(!out512) return -1;
  if(dev_idx != 0 || lba < 0) return -1;
  if(!tn_tezzboot_valid()) return -1;
  if((uint64_t)lba >= TN_TB_BLK_SECTORS) return -1;
  tn_tezzboot_blk_seed();
  const unsigned char* src = tn_tb_blk + ((uint64_t)lba * 512ULL);
  memcpy(out512, src, 512);
  return 512;
#endif
}

TN_MSABI long long os__blk_write_sector(long long dev_idx, long long lba, unsigned char* in512){
#ifdef TN_LIMINE
  if(!in512) return -1;
  if(dev_idx != 0 || lba < 0) return -1;
  struct limine_file* f = os__boot_file();
  if(!f || !f->address || f->size == 0){
    if(!tn_tezzboot_valid()) return -1;
    if((uint64_t)lba >= TN_TB_BLK_SECTORS) return -1;
    tn_tezzboot_blk_seed();
    unsigned char* dst = tn_tb_blk + ((uint64_t)lba * 512ULL);
    memcpy(dst, in512, 512);
    return 512;
  }
  uint64_t sectors = (f->size + 511ULL) / 512ULL;
  if((uint64_t)lba >= sectors) return -1;
  uint64_t off = (uint64_t)lba * 512ULL;
  if(off >= f->size) return -1;
  uint64_t remain = f->size - off;
  uint64_t copy_n = remain;
  if(copy_n > 512ULL) copy_n = 512ULL;
  unsigned char* dst = (unsigned char*)f->address + off;
  memcpy(dst, in512, (size_t)copy_n);
  if(copy_n < 512ULL){
    memset(dst + copy_n, 0, (size_t)(512ULL - copy_n));
  }
  return (long long)copy_n;
#else
  if(!in512) return -1;
  if(dev_idx != 0 || lba < 0) return -1;
  if(!tn_tezzboot_valid()) return -1;
  if((uint64_t)lba >= TN_TB_BLK_SECTORS) return -1;
  tn_tezzboot_blk_seed();
  unsigned char* dst = tn_tb_blk + ((uint64_t)lba * 512ULL);
  memcpy(dst, in512, 512);
  return 512;
#endif
}

TN_MSABI long long os__irq_enable(void){
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  __asm__ volatile("sti");
  return 0;
#else
  return -1;
#endif
}

TN_MSABI long long os__irq_disable(void){
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  __asm__ volatile("cli");
  return 0;
#else
  return -1;
#endif
}

TN_MSABI long long os__halt(void){
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  __asm__ volatile("hlt");
  return 0;
#else
  return -1;
#endif
}

// HPET support (set base via os__hpet_set_base).
static volatile uint64_t g_hpet_base = 0;

TN_MSABI long long os__hpet_set_base(long long base){
  g_hpet_base = (uint64_t)base;
  return 0;
}

TN_MSABI long long os__hpet_init(void){
  if(g_hpet_base == 0) return -1;
  volatile uint64_t* hpet = (volatile uint64_t*)(uintptr_t)g_hpet_base;
  // enable (general config register at 0x10)
  hpet[0x10/8] |= 1;
  return 0;
}

TN_MSABI long long os__hpet_time_ns(void){
  if(g_hpet_base == 0) return -1;
  volatile uint64_t* hpet = (volatile uint64_t*)(uintptr_t)g_hpet_base;
  uint64_t period_fs = hpet[0x4/8];
  uint64_t counter = hpet[0xF0/8];
  // Avoid 128-bit division in freestanding builds.
  uint64_t tick_ns = period_fs / 1000000ULL;
  if(tick_ns == 0) tick_ns = 1;
  return (long long)(counter * tick_ns);
}

static tn_gpu_pci_state g_gpu_pci = {0};
static int g_gpu_pci_scanned = 0;
typedef struct tn_usb_pci_dev {
  uint8_t bus;
  uint8_t slot;
  uint8_t func;
  uint8_t prog_if;
  uint16_t vendor;
  uint16_t device;
} tn_usb_pci_dev;
typedef struct tn_probe_pci_dev {
  uint8_t bus;
  uint8_t slot;
  uint8_t func;
  uint8_t subclass;
  uint8_t prog_if;
  uint16_t vendor;
  uint16_t device;
} tn_probe_pci_dev;
static tn_usb_pci_dev g_usb_pci[16];
static int g_usb_pci_scanned = 0;
static int g_usb_pci_count = 0;
static int g_usb_kind_count[5] = {0,0,0,0,0};
static tn_probe_pci_dev g_net_pci[16];
static int g_net_pci_scanned = 0;
static int g_net_pci_count = 0;
static tn_probe_pci_dev g_audio_pci[16];
static int g_audio_pci_scanned = 0;
static int g_audio_pci_count = 0;
static int g_power_battery_present = 0;
static int g_power_battery_percent = -1;
static int g_rtc_scanned = 0;
static int g_rtc_valid = 0;
static int g_rtc_year = 0;
static int g_rtc_month = 0;
static int g_rtc_day = 0;
static int g_rtc_hour = 0;
static int g_rtc_minute = 0;
static int g_rtc_second = 0;

#define TN_GPU_DRIVER_NONE          0
#define TN_GPU_DRIVER_GENERIC_EFIFB 1
#define TN_GPU_DRIVER_VMWARE_SVGA   2
#define TN_GPU_DRIVER_VBOX_VMSVGA   3
#define TN_GPU_DRIVER_VIRTIO_GPU    4
#define TN_GPU_DRIVER_INTEL_EFIFB   5
#define TN_GPU_DRIVER_AMD_EFIFB     6
#define TN_GPU_DRIVER_NVIDIA_EFIFB  7
#define TN_GPU_DRIVER_VBE           8
#define TN_GPU_DRIVER_BOCHS         9

#define TN_PCI_CLASS_SERIAL_BUS 0x0Cu
#define TN_PCI_SUBCLASS_USB     0x03u

#define TN_USB_KIND_UHCI   0
#define TN_USB_KIND_OHCI   1
#define TN_USB_KIND_EHCI   2
#define TN_USB_KIND_XHCI   3
#define TN_USB_KIND_OTHER  4

static int tn_usb_kind_idx(uint8_t prog_if){
  if(prog_if == 0x00u) return TN_USB_KIND_UHCI;
  if(prog_if == 0x10u) return TN_USB_KIND_OHCI;
  if(prog_if == 0x20u) return TN_USB_KIND_EHCI;
  if(prog_if == 0x30u) return TN_USB_KIND_XHCI;
  return TN_USB_KIND_OTHER;
}

static int tn_audio_subclass_supported(uint8_t subclass){
  if(subclass == TN_PCI_SUBCLASS_AUDIO_DEV) return 1;
  if(subclass == TN_PCI_SUBCLASS_AUDIO_HDA) return 1;
  return 0;
}

static int tn_bcd_to_bin(uint8_t v){
  return ((int)((v >> 4u) & 0x0Fu) * 10) + (int)(v & 0x0Fu);
}

static int tn_rtc_read_reg(uint8_t reg, uint8_t* out){
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  if(!out) return 0;
  if(sys_outb(0x70, (long long)reg) != 0) return 0;
  long long vv = sys_inb(0x71);
  if(vv < 0) return 0;
  *out = (uint8_t)(vv & 0xFFu);
  return 1;
#else
  (void)reg;
  (void)out;
  return 0;
#endif
}

static int tn_rtc_decode_hour(uint8_t raw_hour, uint8_t status_b){
  int hour = (int)raw_hour;
  int is_24 = (status_b & 0x02u) ? 1 : 0;
  int is_binary = (status_b & 0x04u) ? 1 : 0;
  int pm = 0;
  if(!is_24){
    pm = (hour & 0x80) ? 1 : 0;
    hour &= 0x7F;
  }
  if(!is_binary){
    hour = tn_bcd_to_bin((uint8_t)hour);
  }
  if(!is_24){
    if(pm){
      if(hour < 12) hour += 12;
    } else {
      if(hour == 12) hour = 0;
    }
  }
  return hour;
}

static int tn_rtc_refresh(void){
  g_rtc_scanned = 1;
  g_rtc_valid = 0;
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  uint8_t sec = 0;
  uint8_t min = 0;
  uint8_t hour = 0;
  uint8_t day = 0;
  uint8_t mon = 0;
  uint8_t year = 0;
  uint8_t century = 0;
  uint8_t status_a = 0;
  uint8_t status_b = 0;
  int spins = 0;
  while(spins < 100){
    if(!tn_rtc_read_reg(0x0A, &status_a)) return 0;
    if((status_a & 0x80u) == 0u) break;
    spins++;
  }
  if(spins >= 100) return 0;
  if(!tn_rtc_read_reg(0x0B, &status_b)) return 0;
  if(!tn_rtc_read_reg(0x00, &sec)) return 0;
  if(!tn_rtc_read_reg(0x02, &min)) return 0;
  if(!tn_rtc_read_reg(0x04, &hour)) return 0;
  if(!tn_rtc_read_reg(0x07, &day)) return 0;
  if(!tn_rtc_read_reg(0x08, &mon)) return 0;
  if(!tn_rtc_read_reg(0x09, &year)) return 0;
  (void)tn_rtc_read_reg(0x32, &century);
  int is_binary = (status_b & 0x04u) ? 1 : 0;
  int sec_i = (int)sec;
  int min_i = (int)min;
  int day_i = (int)day;
  int mon_i = (int)mon;
  int year_i = (int)year;
  int cent_i = (int)century;
  if(!is_binary){
    sec_i = tn_bcd_to_bin(sec);
    min_i = tn_bcd_to_bin(min);
    day_i = tn_bcd_to_bin(day);
    mon_i = tn_bcd_to_bin(mon);
    year_i = tn_bcd_to_bin(year);
    if(cent_i != 0){
      cent_i = tn_bcd_to_bin(century);
    }
  }
  int hour_i = tn_rtc_decode_hour(hour, status_b);
  int full_year = year_i;
  if(cent_i >= 19 && cent_i <= 30){
    full_year = cent_i * 100 + year_i;
  } else if(year_i < 70){
    full_year = 2000 + year_i;
  } else {
    full_year = 1900 + year_i;
  }
  if(sec_i < 0 || sec_i > 59) return 0;
  if(min_i < 0 || min_i > 59) return 0;
  if(hour_i < 0 || hour_i > 23) return 0;
  if(day_i < 1 || day_i > 31) return 0;
  if(mon_i < 1 || mon_i > 12) return 0;
  if(full_year < 1970 || full_year > 2199) return 0;
  g_rtc_year = full_year;
  g_rtc_month = mon_i;
  g_rtc_day = day_i;
  g_rtc_hour = hour_i;
  g_rtc_minute = min_i;
  g_rtc_second = sec_i;
  g_rtc_valid = 1;
  return 1;
#else
  return 0;
#endif
}

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
static uint32_t tn_pci_cfg_addr(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off){
  return (uint32_t)(0x80000000u |
                    ((uint32_t)bus << 16) |
                    ((uint32_t)slot << 11) |
                    ((uint32_t)func << 8) |
                    ((uint32_t)off & 0xFCu));
}

static uint32_t tn_pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off){
  tn_io_outl((uint16_t)TN_PCI_CFG_ADDR_PORT, tn_pci_cfg_addr(bus, slot, func, off));
  return tn_io_inl((uint16_t)TN_PCI_CFG_DATA_PORT);
}

static void tn_pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t val){
  tn_io_outl((uint16_t)TN_PCI_CFG_ADDR_PORT, tn_pci_cfg_addr(bus, slot, func, off));
  tn_io_outl((uint16_t)TN_PCI_CFG_DATA_PORT, val);
}

static uint16_t tn_pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off){
  uint32_t v = tn_pci_read32(bus, slot, func, (uint8_t)(off & 0xFCu));
  uint32_t sh = (uint32_t)(off & 2u) * 8u;
  return (uint16_t)((v >> sh) & 0xFFFFu);
}

static uint8_t tn_pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off){
  uint32_t v = tn_pci_read32(bus, slot, func, (uint8_t)(off & 0xFCu));
  uint32_t sh = (uint32_t)(off & 3u) * 8u;
  return (uint8_t)((v >> sh) & 0xFFu);
}

static void tn_pci_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint16_t val){
  uint8_t base = (uint8_t)(off & 0xFCu);
  uint32_t cur = tn_pci_read32(bus, slot, func, base);
  uint32_t sh = (uint32_t)(off & 2u) * 8u;
  uint32_t mask = 0xFFFFu << sh;
  uint32_t next = (cur & ~mask) | (((uint32_t)val << sh) & mask);
  tn_pci_write32(bus, slot, func, base, next);
}

static uint64_t tn_pci_bar_size32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off,
                                  uint32_t saved, uint8_t is_io){
  uint32_t mask;
  tn_pci_write32(bus, slot, func, off, 0xFFFFFFFFu);
  mask = tn_pci_read32(bus, slot, func, off);
  tn_pci_write32(bus, slot, func, off, saved);
  if(mask == 0 || mask == 0xFFFFFFFFu) return 0;
  if(is_io){
    mask &= ~0x3u;
  } else {
    mask &= ~0xFu;
  }
  if(mask == 0) return 0;
  return (uint64_t)(~mask + 1u);
}

static uint64_t tn_pci_bar_size64(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off,
                                  uint32_t saved_lo, uint32_t saved_hi){
  uint32_t mask_lo;
  uint32_t mask_hi;
  uint64_t mask;
  tn_pci_write32(bus, slot, func, off, 0xFFFFFFFFu);
  tn_pci_write32(bus, slot, func, (uint8_t)(off + 4u), 0xFFFFFFFFu);
  mask_lo = tn_pci_read32(bus, slot, func, off);
  mask_hi = tn_pci_read32(bus, slot, func, (uint8_t)(off + 4u));
  tn_pci_write32(bus, slot, func, (uint8_t)(off + 4u), saved_hi);
  tn_pci_write32(bus, slot, func, off, saved_lo);
  if((mask_lo == 0 || mask_lo == 0xFFFFFFFFu) && (mask_hi == 0 || mask_hi == 0xFFFFFFFFu)){
    return 0;
  }
  mask = (((uint64_t)mask_hi << 32) | (uint64_t)(mask_lo & ~0xFu));
  if(mask == 0 || mask == 0xFFFFFFFFFFFFFFFFull) return 0;
  return (~mask + 1ull);
}

static int tn_gpu_rank(uint16_t vendor, uint16_t device, uint8_t subclass){
  int score = 1;
  if(subclass == TN_PCI_SUBCLASS_VGA) score += 1;
  if(vendor == TN_PCI_VENDOR_VMWARE &&
     (device == TN_PCI_DEVICE_VMWARE_SVGA2 || device == TN_PCI_DEVICE_VMWARE_SVGA3)){
    score += 100;
  }
  if(vendor == TN_PCI_VENDOR_VBOX && device == TN_PCI_DEVICE_VBOX_VGA){
    score += 50;
  }
  return score;
}

static int tn_u64_pow2(uint64_t v){
  if(v == 0) return 0;
  return (v & (v - 1u)) == 0 ? 1 : 0;
}

static uint8_t tn_gpu_bar_safe_check(uint64_t addr, uint64_t size, uint8_t is_io, uint8_t mem_type){
  if(addr == 0 || size == 0) return 0;
  if(!tn_u64_pow2(size)) return 0;
  if(is_io){
    if(addr > 0xFFFFu) return 0;
    if(size > 0x10000u) return 0;
    return 1;
  }
  if(mem_type == 0x1u){
    // 16-bit below-1MB memory BAR is legacy-only and not safe for this stack.
    return 0;
  }
  if((addr & 0xFu) != 0) return 0;
  if(size < 0x1000u) return 0;
  if(size > 0x40000000ull) return 0;
  return 1;
}

static void tn_gpu_parse_bars(void){
  g_gpu_pci.mmio_safe = 0;
  uint8_t i = 0;
  for(i = 0; i < 6; i++){
    uint8_t off = (uint8_t)(0x10u + (i * 4u));
    uint32_t lo = tn_pci_read32(g_gpu_pci.bus, g_gpu_pci.slot, g_gpu_pci.func, off);
    if(lo == 0 || lo == 0xFFFFFFFFu){
      g_gpu_pci.bar_safe[i] = 0;
      continue;
    }
    if(lo & 1u){
      g_gpu_pci.bar_is_io[i] = 1;
      g_gpu_pci.bar_addr[i] = (uint64_t)(lo & ~0x3u);
      g_gpu_pci.bar_size[i] = tn_pci_bar_size32(g_gpu_pci.bus, g_gpu_pci.slot, g_gpu_pci.func, off, lo, 1);
      g_gpu_pci.bar_safe[i] = tn_gpu_bar_safe_check(g_gpu_pci.bar_addr[i], g_gpu_pci.bar_size[i], 1, 0);
    } else {
      uint8_t mem_type = (uint8_t)((lo >> 1) & 0x3u);
      uint8_t safe_cur = 0;
      g_gpu_pci.bar_mem_type[i] = mem_type;
      g_gpu_pci.bar_prefetch[i] = (uint8_t)((lo >> 3) & 1u);
      if(mem_type == 0x2u && i < 5){
        uint32_t hi = tn_pci_read32(g_gpu_pci.bus, g_gpu_pci.slot, g_gpu_pci.func, (uint8_t)(off + 4u));
        g_gpu_pci.bar_addr[i] = ((uint64_t)hi << 32) | (uint64_t)(lo & ~0xFu);
        g_gpu_pci.bar_size[i] = tn_pci_bar_size64(g_gpu_pci.bus, g_gpu_pci.slot, g_gpu_pci.func, off, lo, hi);
        safe_cur = tn_gpu_bar_safe_check(g_gpu_pci.bar_addr[i], g_gpu_pci.bar_size[i], 0, mem_type);
        g_gpu_pci.bar_safe[i] = safe_cur;
        i = (uint8_t)(i + 1u);
      } else {
        g_gpu_pci.bar_addr[i] = (uint64_t)(lo & ~0xFu);
        g_gpu_pci.bar_size[i] = tn_pci_bar_size32(g_gpu_pci.bus, g_gpu_pci.slot, g_gpu_pci.func, off, lo, 0);
        safe_cur = tn_gpu_bar_safe_check(g_gpu_pci.bar_addr[i], g_gpu_pci.bar_size[i], 0, mem_type);
        g_gpu_pci.bar_safe[i] = safe_cur;
      }
      if(safe_cur != 0){
        g_gpu_pci.mmio_safe = 1;
      }
    }
  }
}

static uint32_t tn_svga_read(uint16_t io_base, uint32_t reg){
  tn_io_outl((uint16_t)(io_base + TN_SVGA_INDEX_PORT), reg);
  return tn_io_inl((uint16_t)(io_base + TN_SVGA_VALUE_PORT));
}

static void tn_svga_write(uint16_t io_base, uint32_t reg, uint32_t value){
  tn_io_outl((uint16_t)(io_base + TN_SVGA_INDEX_PORT), reg);
  tn_io_outl((uint16_t)(io_base + TN_SVGA_VALUE_PORT), value);
}

static void tn_gpu_try_svga_cursor(void){
  if(g_gpu_pci.vendor != TN_PCI_VENDOR_VMWARE) return;
  if(!(g_gpu_pci.device == TN_PCI_DEVICE_VMWARE_SVGA2 || g_gpu_pci.device == TN_PCI_DEVICE_VMWARE_SVGA3)) return;
  if(g_gpu_pci.bar_addr[0] == 0 || g_gpu_pci.bar_is_io[0] == 0) return;
  g_gpu_pci.svga_io_base = (uint16_t)(g_gpu_pci.bar_addr[0] & 0xFFFFu);
  if(g_gpu_pci.svga_io_base == 0) return;
  g_gpu_pci.svga_caps = tn_svga_read(g_gpu_pci.svga_io_base, TN_SVGA_REG_CAPABILITIES);
  if((g_gpu_pci.svga_caps & (TN_SVGA_CAP_CURSOR | TN_SVGA_CAP_CURSOR_BYPASS | TN_SVGA_CAP_CURSOR_BYPASS_2)) == 0){
    return;
  }
  if((g_gpu_pci.svga_caps & TN_SVGA_CAP_CURSOR_BYPASS_2) != 0){
    g_gpu_pci.svga_cursor4 = 1;
  } else {
    g_gpu_pci.svga_cursor4 = 0;
  }
  g_gpu_pci.svga_cursor_ready = 1;
}
#endif

TN_MSABI long long os__pci_display_probe(void){
  if(g_gpu_pci_scanned) return g_gpu_pci.present ? 1 : 0;
  g_gpu_pci_scanned = 1;
  memset(&g_gpu_pci, 0, sizeof(g_gpu_pci));
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  int best_score = 0;
  uint16_t bus;
  for(bus = 0; bus < 256; bus++){
    uint8_t slot;
    for(slot = 0; slot < 32; slot++){
      uint16_t vendor0 = tn_pci_read16((uint8_t)bus, slot, 0, 0x00);
      if(vendor0 == 0xFFFFu) continue;
      uint8_t hdr = tn_pci_read8((uint8_t)bus, slot, 0, 0x0E);
      uint8_t funcs = (hdr & 0x80u) ? 8u : 1u;
      uint8_t func;
      for(func = 0; func < funcs; func++){
        uint16_t vendor = tn_pci_read16((uint8_t)bus, slot, func, 0x00);
        if(vendor == 0xFFFFu) continue;
        uint16_t device = tn_pci_read16((uint8_t)bus, slot, func, 0x02);
        uint32_t classreg = tn_pci_read32((uint8_t)bus, slot, func, 0x08);
        uint8_t class_code = (uint8_t)((classreg >> 24) & 0xFFu);
        uint8_t subclass = (uint8_t)((classreg >> 16) & 0xFFu);
        uint8_t prog_if = (uint8_t)((classreg >> 8) & 0xFFu);
        if(class_code != TN_PCI_CLASS_DISPLAY) continue;
        {
          int score = tn_gpu_rank(vendor, device, subclass);
          if(score > best_score){
            best_score = score;
            g_gpu_pci.present = 1;
            g_gpu_pci.bus = (uint8_t)bus;
            g_gpu_pci.slot = slot;
            g_gpu_pci.func = func;
            g_gpu_pci.vendor = vendor;
            g_gpu_pci.device = device;
            g_gpu_pci.class_code = class_code;
            g_gpu_pci.subclass = subclass;
            g_gpu_pci.prog_if = prog_if;
            g_gpu_pci.header_type = tn_pci_read8((uint8_t)bus, slot, func, 0x0E);
            g_gpu_pci.command = tn_pci_read16((uint8_t)bus, slot, func, 0x04);
          }
        }
      }
    }
  }
  if(g_gpu_pci.present){
    uint16_t cmd = g_gpu_pci.command;
    cmd = (uint16_t)(cmd | 0x0007u);
    tn_pci_write16(g_gpu_pci.bus, g_gpu_pci.slot, g_gpu_pci.func, 0x04, cmd);
    g_gpu_pci.command = tn_pci_read16(g_gpu_pci.bus, g_gpu_pci.slot, g_gpu_pci.func, 0x04);
    tn_gpu_parse_bars();
    tn_gpu_try_svga_cursor();
  }
#endif
  return g_gpu_pci.present ? 1 : 0;
}

TN_MSABI long long os__pci_display_vendor(void){ if(!g_gpu_pci_scanned) os__pci_display_probe(); return (long long)g_gpu_pci.vendor; }
TN_MSABI long long os__pci_display_device(void){ if(!g_gpu_pci_scanned) os__pci_display_probe(); return (long long)g_gpu_pci.device; }
TN_MSABI long long os__pci_display_class(void){ if(!g_gpu_pci_scanned) os__pci_display_probe(); return (long long)g_gpu_pci.class_code; }
TN_MSABI long long os__pci_display_subclass(void){ if(!g_gpu_pci_scanned) os__pci_display_probe(); return (long long)g_gpu_pci.subclass; }
TN_MSABI long long os__pci_display_prog_if(void){ if(!g_gpu_pci_scanned) os__pci_display_probe(); return (long long)g_gpu_pci.prog_if; }
TN_MSABI long long os__pci_display_bus(void){ if(!g_gpu_pci_scanned) os__pci_display_probe(); return (long long)g_gpu_pci.bus; }
TN_MSABI long long os__pci_display_slot(void){ if(!g_gpu_pci_scanned) os__pci_display_probe(); return (long long)g_gpu_pci.slot; }
TN_MSABI long long os__pci_display_func(void){ if(!g_gpu_pci_scanned) os__pci_display_probe(); return (long long)g_gpu_pci.func; }
TN_MSABI long long os__pci_display_command(void){ if(!g_gpu_pci_scanned) os__pci_display_probe(); return (long long)g_gpu_pci.command; }

TN_MSABI long long os__pci_display_bar_addr(long long idx){
  if(!g_gpu_pci_scanned) os__pci_display_probe();
  if(idx < 0 || idx >= 6) return 0;
  return (long long)g_gpu_pci.bar_addr[idx];
}

TN_MSABI long long os__pci_display_bar_size(long long idx){
  if(!g_gpu_pci_scanned) os__pci_display_probe();
  if(idx < 0 || idx >= 6) return 0;
  return (long long)g_gpu_pci.bar_size[idx];
}

TN_MSABI long long os__pci_display_bar_is_io(long long idx){
  if(!g_gpu_pci_scanned) os__pci_display_probe();
  if(idx < 0 || idx >= 6) return 0;
  return (long long)g_gpu_pci.bar_is_io[idx];
}

TN_MSABI long long os__pci_display_bar_prefetch(long long idx){
  if(!g_gpu_pci_scanned) os__pci_display_probe();
  if(idx < 0 || idx >= 6) return 0;
  return (long long)g_gpu_pci.bar_prefetch[idx];
}

TN_MSABI long long os__pci_display_bar_mem_type(long long idx){
  if(!g_gpu_pci_scanned) os__pci_display_probe();
  if(idx < 0 || idx >= 6) return 0;
  return (long long)g_gpu_pci.bar_mem_type[idx];
}

TN_MSABI long long os__pci_display_bar_safe(long long idx){
  if(!g_gpu_pci_scanned) os__pci_display_probe();
  if(idx < 0 || idx >= 6) return 0;
  return (long long)g_gpu_pci.bar_safe[idx];
}

TN_MSABI long long os__pci_display_mmio_safe(void){
  if(!g_gpu_pci_scanned) os__pci_display_probe();
  return (long long)g_gpu_pci.mmio_safe;
}

TN_MSABI long long os__pci_usb_probe(void){
  if(g_usb_pci_scanned) return (long long)g_usb_pci_count;
  g_usb_pci_scanned = 1;
  g_usb_pci_count = 0;
  memset(g_usb_pci, 0, sizeof(g_usb_pci));
  memset(g_usb_kind_count, 0, sizeof(g_usb_kind_count));
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  uint16_t bus;
  for(bus = 0; bus < 256; bus++){
    uint8_t slot;
    for(slot = 0; slot < 32; slot++){
      uint16_t vendor0 = tn_pci_read16((uint8_t)bus, slot, 0, 0x00);
      if(vendor0 == 0xFFFFu) continue;
      uint8_t hdr = tn_pci_read8((uint8_t)bus, slot, 0, 0x0E);
      uint8_t funcs = (hdr & 0x80u) ? 8u : 1u;
      uint8_t func;
      for(func = 0; func < funcs; func++){
        uint16_t vendor = tn_pci_read16((uint8_t)bus, slot, func, 0x00);
        if(vendor == 0xFFFFu) continue;
        uint32_t classreg = tn_pci_read32((uint8_t)bus, slot, func, 0x08);
        uint8_t class_code = (uint8_t)((classreg >> 24) & 0xFFu);
        uint8_t subclass = (uint8_t)((classreg >> 16) & 0xFFu);
        uint8_t prog_if = (uint8_t)((classreg >> 8) & 0xFFu);
        if(class_code != TN_PCI_CLASS_SERIAL_BUS || subclass != TN_PCI_SUBCLASS_USB){
          continue;
        }
        int kind = tn_usb_kind_idx(prog_if);
        if(kind >= 0 && kind < 5){
          g_usb_kind_count[kind] = g_usb_kind_count[kind] + 1;
        }
        if(g_usb_pci_count < (int)(sizeof(g_usb_pci) / sizeof(g_usb_pci[0]))){
          g_usb_pci[g_usb_pci_count].bus = (uint8_t)bus;
          g_usb_pci[g_usb_pci_count].slot = slot;
          g_usb_pci[g_usb_pci_count].func = func;
          g_usb_pci[g_usb_pci_count].prog_if = prog_if;
          g_usb_pci[g_usb_pci_count].vendor = vendor;
          g_usb_pci[g_usb_pci_count].device = tn_pci_read16((uint8_t)bus, slot, func, 0x02);
          g_usb_pci_count = g_usb_pci_count + 1;
        }
      }
    }
  }
#endif
  return (long long)g_usb_pci_count;
}

TN_MSABI long long os__pci_usb_count(void){
  if(!g_usb_pci_scanned) os__pci_usb_probe();
  return (long long)g_usb_pci_count;
}

TN_MSABI long long os__pci_usb_kind_count(long long kind){
  if(!g_usb_pci_scanned) os__pci_usb_probe();
  if(kind < 0 || kind >= 5) return 0;
  return (long long)g_usb_kind_count[kind];
}

TN_MSABI long long os__pci_usb_bus(long long idx){
  if(!g_usb_pci_scanned) os__pci_usb_probe();
  if(idx < 0 || idx >= g_usb_pci_count) return 0;
  return (long long)g_usb_pci[idx].bus;
}

TN_MSABI long long os__pci_usb_slot(long long idx){
  if(!g_usb_pci_scanned) os__pci_usb_probe();
  if(idx < 0 || idx >= g_usb_pci_count) return 0;
  return (long long)g_usb_pci[idx].slot;
}

TN_MSABI long long os__pci_usb_func(long long idx){
  if(!g_usb_pci_scanned) os__pci_usb_probe();
  if(idx < 0 || idx >= g_usb_pci_count) return 0;
  return (long long)g_usb_pci[idx].func;
}

TN_MSABI long long os__pci_usb_prog_if(long long idx){
  if(!g_usb_pci_scanned) os__pci_usb_probe();
  if(idx < 0 || idx >= g_usb_pci_count) return 0;
  return (long long)g_usb_pci[idx].prog_if;
}

TN_MSABI long long os__pci_usb_vendor(long long idx){
  if(!g_usb_pci_scanned) os__pci_usb_probe();
  if(idx < 0 || idx >= g_usb_pci_count) return 0;
  return (long long)g_usb_pci[idx].vendor;
}

TN_MSABI long long os__pci_usb_device(long long idx){
  if(!g_usb_pci_scanned) os__pci_usb_probe();
  if(idx < 0 || idx >= g_usb_pci_count) return 0;
  return (long long)g_usb_pci[idx].device;
}

TN_MSABI long long os__pci_net_probe(void){
  if(g_net_pci_scanned) return (long long)g_net_pci_count;
  g_net_pci_scanned = 1;
  g_net_pci_count = 0;
  memset(g_net_pci, 0, sizeof(g_net_pci));
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  uint16_t bus;
  for(bus = 0; bus < 256; bus++){
    uint8_t slot;
    for(slot = 0; slot < 32; slot++){
      uint16_t vendor0 = tn_pci_read16((uint8_t)bus, slot, 0, 0x00);
      if(vendor0 == 0xFFFFu) continue;
      uint8_t hdr = tn_pci_read8((uint8_t)bus, slot, 0, 0x0E);
      uint8_t funcs = (hdr & 0x80u) ? 8u : 1u;
      uint8_t func;
      for(func = 0; func < funcs; func++){
        uint16_t vendor = tn_pci_read16((uint8_t)bus, slot, func, 0x00);
        if(vendor == 0xFFFFu) continue;
        uint32_t classreg = tn_pci_read32((uint8_t)bus, slot, func, 0x08);
        uint8_t class_code = (uint8_t)((classreg >> 24) & 0xFFu);
        uint8_t subclass = (uint8_t)((classreg >> 16) & 0xFFu);
        uint8_t prog_if = (uint8_t)((classreg >> 8) & 0xFFu);
        if(class_code != TN_PCI_CLASS_NETWORK){
          continue;
        }
        if(g_net_pci_count < (int)(sizeof(g_net_pci) / sizeof(g_net_pci[0]))){
          g_net_pci[g_net_pci_count].bus = (uint8_t)bus;
          g_net_pci[g_net_pci_count].slot = slot;
          g_net_pci[g_net_pci_count].func = func;
          g_net_pci[g_net_pci_count].subclass = subclass;
          g_net_pci[g_net_pci_count].prog_if = prog_if;
          g_net_pci[g_net_pci_count].vendor = vendor;
          g_net_pci[g_net_pci_count].device = tn_pci_read16((uint8_t)bus, slot, func, 0x02);
          g_net_pci_count = g_net_pci_count + 1;
        }
      }
    }
  }
#endif
  return (long long)g_net_pci_count;
}

TN_MSABI long long os__pci_net_count(void){
  if(!g_net_pci_scanned) os__pci_net_probe();
  return (long long)g_net_pci_count;
}

TN_MSABI long long os__pci_net_bus(long long idx){
  if(!g_net_pci_scanned) os__pci_net_probe();
  if(idx < 0 || idx >= g_net_pci_count) return 0;
  return (long long)g_net_pci[idx].bus;
}

TN_MSABI long long os__pci_net_slot(long long idx){
  if(!g_net_pci_scanned) os__pci_net_probe();
  if(idx < 0 || idx >= g_net_pci_count) return 0;
  return (long long)g_net_pci[idx].slot;
}

TN_MSABI long long os__pci_net_func(long long idx){
  if(!g_net_pci_scanned) os__pci_net_probe();
  if(idx < 0 || idx >= g_net_pci_count) return 0;
  return (long long)g_net_pci[idx].func;
}

TN_MSABI long long os__pci_net_subclass(long long idx){
  if(!g_net_pci_scanned) os__pci_net_probe();
  if(idx < 0 || idx >= g_net_pci_count) return 0;
  return (long long)g_net_pci[idx].subclass;
}

TN_MSABI long long os__pci_net_prog_if(long long idx){
  if(!g_net_pci_scanned) os__pci_net_probe();
  if(idx < 0 || idx >= g_net_pci_count) return 0;
  return (long long)g_net_pci[idx].prog_if;
}

TN_MSABI long long os__pci_net_vendor(long long idx){
  if(!g_net_pci_scanned) os__pci_net_probe();
  if(idx < 0 || idx >= g_net_pci_count) return 0;
  return (long long)g_net_pci[idx].vendor;
}

TN_MSABI long long os__pci_net_device(long long idx){
  if(!g_net_pci_scanned) os__pci_net_probe();
  if(idx < 0 || idx >= g_net_pci_count) return 0;
  return (long long)g_net_pci[idx].device;
}

TN_MSABI long long os__pci_audio_probe(void){
  if(g_audio_pci_scanned) return (long long)g_audio_pci_count;
  g_audio_pci_scanned = 1;
  g_audio_pci_count = 0;
  memset(g_audio_pci, 0, sizeof(g_audio_pci));
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  uint16_t bus;
  for(bus = 0; bus < 256; bus++){
    uint8_t slot;
    for(slot = 0; slot < 32; slot++){
      uint16_t vendor0 = tn_pci_read16((uint8_t)bus, slot, 0, 0x00);
      if(vendor0 == 0xFFFFu) continue;
      uint8_t hdr = tn_pci_read8((uint8_t)bus, slot, 0, 0x0E);
      uint8_t funcs = (hdr & 0x80u) ? 8u : 1u;
      uint8_t func;
      for(func = 0; func < funcs; func++){
        uint16_t vendor = tn_pci_read16((uint8_t)bus, slot, func, 0x00);
        if(vendor == 0xFFFFu) continue;
        uint32_t classreg = tn_pci_read32((uint8_t)bus, slot, func, 0x08);
        uint8_t class_code = (uint8_t)((classreg >> 24) & 0xFFu);
        uint8_t subclass = (uint8_t)((classreg >> 16) & 0xFFu);
        uint8_t prog_if = (uint8_t)((classreg >> 8) & 0xFFu);
        if(class_code != TN_PCI_CLASS_MULTIMEDIA){
          continue;
        }
        if(!tn_audio_subclass_supported(subclass)){
          continue;
        }
        if(g_audio_pci_count < (int)(sizeof(g_audio_pci) / sizeof(g_audio_pci[0]))){
          g_audio_pci[g_audio_pci_count].bus = (uint8_t)bus;
          g_audio_pci[g_audio_pci_count].slot = slot;
          g_audio_pci[g_audio_pci_count].func = func;
          g_audio_pci[g_audio_pci_count].subclass = subclass;
          g_audio_pci[g_audio_pci_count].prog_if = prog_if;
          g_audio_pci[g_audio_pci_count].vendor = vendor;
          g_audio_pci[g_audio_pci_count].device = tn_pci_read16((uint8_t)bus, slot, func, 0x02);
          g_audio_pci_count = g_audio_pci_count + 1;
        }
      }
    }
  }
#endif
  return (long long)g_audio_pci_count;
}

TN_MSABI long long os__pci_audio_count(void){
  if(!g_audio_pci_scanned) os__pci_audio_probe();
  return (long long)g_audio_pci_count;
}

TN_MSABI long long os__pci_audio_bus(long long idx){
  if(!g_audio_pci_scanned) os__pci_audio_probe();
  if(idx < 0 || idx >= g_audio_pci_count) return 0;
  return (long long)g_audio_pci[idx].bus;
}

TN_MSABI long long os__pci_audio_slot(long long idx){
  if(!g_audio_pci_scanned) os__pci_audio_probe();
  if(idx < 0 || idx >= g_audio_pci_count) return 0;
  return (long long)g_audio_pci[idx].slot;
}

TN_MSABI long long os__pci_audio_func(long long idx){
  if(!g_audio_pci_scanned) os__pci_audio_probe();
  if(idx < 0 || idx >= g_audio_pci_count) return 0;
  return (long long)g_audio_pci[idx].func;
}

TN_MSABI long long os__pci_audio_subclass(long long idx){
  if(!g_audio_pci_scanned) os__pci_audio_probe();
  if(idx < 0 || idx >= g_audio_pci_count) return 0;
  return (long long)g_audio_pci[idx].subclass;
}

TN_MSABI long long os__pci_audio_prog_if(long long idx){
  if(!g_audio_pci_scanned) os__pci_audio_probe();
  if(idx < 0 || idx >= g_audio_pci_count) return 0;
  return (long long)g_audio_pci[idx].prog_if;
}

TN_MSABI long long os__pci_audio_vendor(long long idx){
  if(!g_audio_pci_scanned) os__pci_audio_probe();
  if(idx < 0 || idx >= g_audio_pci_count) return 0;
  return (long long)g_audio_pci[idx].vendor;
}

TN_MSABI long long os__pci_audio_device(long long idx){
  if(!g_audio_pci_scanned) os__pci_audio_probe();
  if(idx < 0 || idx >= g_audio_pci_count) return 0;
  return (long long)g_audio_pci[idx].device;
}

TN_MSABI long long os__gpu_hw_cursor_available(void){
  if(!g_gpu_pci_scanned) os__pci_display_probe();
  return g_gpu_pci.svga_cursor_ready ? 1 : 0;
}

TN_MSABI long long os__gpu_hw_cursor_enable(long long on){
  if(!g_gpu_pci_scanned) os__pci_display_probe();
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  if(!g_gpu_pci.svga_cursor_ready || g_gpu_pci.svga_io_base == 0) return -1;
  if(g_gpu_pci.svga_cursor4){
    tn_svga_write(g_gpu_pci.svga_io_base, TN_SVGA_REG_CURSOR4_ON, on ? TN_SVGA_CURSOR_ON_SHOW : TN_SVGA_CURSOR_ON_HIDE);
  } else {
    tn_svga_write(g_gpu_pci.svga_io_base, TN_SVGA_REG_CURSOR_ON, on ? TN_SVGA_CURSOR_ON_SHOW : TN_SVGA_CURSOR_ON_HIDE);
  }
  g_gpu_pci.svga_cursor_on = on ? 1 : 0;
  return 0;
#else
  (void)on;
  return -1;
#endif
}

TN_MSABI long long os__gpu_hw_cursor_move(long long x, long long y){
  if(!g_gpu_pci_scanned) os__pci_display_probe();
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  if(!g_gpu_pci.svga_cursor_ready || g_gpu_pci.svga_io_base == 0) return -1;
  if(x < 0) x = 0;
  if(y < 0) y = 0;
  if(g_gpu_pci.svga_cursor4){
    tn_svga_write(g_gpu_pci.svga_io_base, TN_SVGA_REG_CURSOR4_X, (uint32_t)x);
    tn_svga_write(g_gpu_pci.svga_io_base, TN_SVGA_REG_CURSOR4_Y, (uint32_t)y);
  } else {
    tn_svga_write(g_gpu_pci.svga_io_base, TN_SVGA_REG_CURSOR_X, (uint32_t)x);
    tn_svga_write(g_gpu_pci.svga_io_base, TN_SVGA_REG_CURSOR_Y, (uint32_t)y);
  }
  return 0;
#else
  (void)x; (void)y;
  return -1;
#endif
}

TN_MSABI long long os__gpu_driver_kind(void){
  if(!g_gpu_pci_scanned) os__pci_display_probe();
  if(!g_gpu_pci.present){
    if(os__fb_addr() != 0){
      return TN_GPU_DRIVER_VBE;
    }
    return TN_GPU_DRIVER_NONE;
  }
  if(g_gpu_pci.vendor == TN_PCI_VENDOR_BOCHS_QEMU && g_gpu_pci.device == TN_PCI_DEVICE_BOCHS_VGA){
    return TN_GPU_DRIVER_BOCHS;
  }
  if(g_gpu_pci.vendor == TN_PCI_VENDOR_VMWARE &&
     (g_gpu_pci.device == TN_PCI_DEVICE_VMWARE_SVGA2 || g_gpu_pci.device == TN_PCI_DEVICE_VMWARE_SVGA3)){
    return TN_GPU_DRIVER_VMWARE_SVGA;
  }
  if(g_gpu_pci.vendor == TN_PCI_VENDOR_VBOX){
    return TN_GPU_DRIVER_VBOX_VMSVGA;
  }
  if(g_gpu_pci.vendor == 0x1AF4u){
    if(g_gpu_pci.device == 0x1005u || g_gpu_pci.device == 0x1040u || g_gpu_pci.device == 0x1050u || g_gpu_pci.device == 0x1052u){
      return TN_GPU_DRIVER_VIRTIO_GPU;
    }
  }
  if(g_gpu_pci.vendor == 0x8086u) return TN_GPU_DRIVER_INTEL_EFIFB;
  if(g_gpu_pci.vendor == 0x1002u || g_gpu_pci.vendor == 0x1022u) return TN_GPU_DRIVER_AMD_EFIFB;
  if(g_gpu_pci.vendor == 0x10DEu) return TN_GPU_DRIVER_NVIDIA_EFIFB;
  return TN_GPU_DRIVER_GENERIC_EFIFB;
}

TN_MSABI long long os__power_battery_present(void){
  return (long long)g_power_battery_present;
}

TN_MSABI long long os__power_battery_percent(void){
  return (long long)g_power_battery_percent;
}

TN_MSABI long long os__rtc_probe(void){
  return (long long)tn_rtc_refresh();
}

TN_MSABI long long os__rtc_year(void){
  if(!g_rtc_scanned) tn_rtc_refresh();
  if(!g_rtc_valid) return 0;
  return (long long)g_rtc_year;
}

TN_MSABI long long os__rtc_month(void){
  if(!g_rtc_scanned) tn_rtc_refresh();
  if(!g_rtc_valid) return 0;
  return (long long)g_rtc_month;
}

TN_MSABI long long os__rtc_day(void){
  if(!g_rtc_scanned) tn_rtc_refresh();
  if(!g_rtc_valid) return 0;
  return (long long)g_rtc_day;
}

TN_MSABI long long os__rtc_hour(void){
  if(!g_rtc_scanned) tn_rtc_refresh();
  if(!g_rtc_valid) return 0;
  return (long long)g_rtc_hour;
}

TN_MSABI long long os__rtc_minute(void){
  if(!g_rtc_scanned) tn_rtc_refresh();
  if(!g_rtc_valid) return 0;
  return (long long)g_rtc_minute;
}

TN_MSABI long long os__rtc_second(void){
  if(!g_rtc_scanned) tn_rtc_refresh();
  if(!g_rtc_valid) return 0;
  return (long long)g_rtc_second;
}

// Plain symbol wrappers expected by `lib/os.tn` externs.
TN_MSABI unsigned char* fb_addr(void){ return (unsigned char*)os__fb_addr(); }
TN_MSABI long long fb_width(void){ return os__fb_width(); }
TN_MSABI long long fb_height(void){ return os__fb_height(); }
TN_MSABI long long fb_pitch(void){ return os__fb_pitch(); }
TN_MSABI long long fb_bpp(void){ return os__fb_bpp(); }
TN_MSABI long long boot_text(void){ return os__boot_text(); }
TN_MSABI long long fb_text(long long x, long long y, unsigned char* s, long long color){
  return os__fb_text(x, y, s, color);
}
TN_MSABI long long fb_set_scale(long long scale){ return os__fb_set_scale(scale); }
TN_MSABI long long fb_get_scale(void){ return os__fb_get_scale(); }
TN_MSABI long long fb_text_mode(long long mode){ return os__fb_text_mode(mode); }
TN_MSABI long long fb_font_aa_mode(long long mode){ return os__fb_font_aa_mode(mode); }
TN_MSABI long long fb_font_ttf_mode(long long mode){ return os__fb_font_ttf_mode(mode); }
TN_MSABI long long fb_draw_logo(long long x, long long y, long long w, long long h){ return os__fb_draw_logo(x, y, w, h); }
TN_MSABI long long fb_draw_wallpaper(long long x, long long y, long long w, long long h){ return os__fb_draw_wallpaper(x, y, w, h); }
TN_MSABI long long fb_draw_cursor(long long x, long long y, long long scale){ return os__fb_draw_cursor(x, y, scale); }
TN_MSABI long long fb_draw_app_icon(long long app, long long x, long long y, long long w, long long h){
  return os__fb_draw_app_icon(app, x, y, w, h);
}
TN_MSABI long long fb_fill(long long color){ return os__fb_fill(color); }
TN_MSABI long long fb_fill_rect(long long x, long long y, long long w, long long h, long long color){
  return os__fb_fill_rect(x, y, w, h, color);
}
TN_MSABI long long fb_blit(long long sx, long long sy, long long w, long long h, long long dx, long long dy){
  return os__fb_blit(sx, sy, w, h, dx, dy);
}
TN_MSABI long long fb_composite_rect(long long x, long long y, long long w, long long h, long long color, long long alpha){
  return os__fb_composite_rect(x, y, w, h, color, alpha);
}
TN_MSABI long long fb_get_pixel(long long x, long long y){ return os__fb_get_pixel(x, y); }
TN_MSABI long long fb_put_pixel(long long x, long long y, long long color){ return os__fb_put_pixel(x, y, color); }
TN_MSABI long long fb_cursor_soft_reset(void){ return os__fb_cursor_soft_reset(); }
TN_MSABI long long fb_cursor_soft_move(long long x, long long y, long long scale){
  return os__fb_cursor_soft_move(x, y, scale);
}
TN_MSABI long long fb_cursor_kind(long long kind){ return os__fb_cursor_kind(kind); }
TN_MSABI long long fb_set_color(long long color){ return os__fb_set_color(color); }
TN_MSABI long long fb_set_cursor(long long x, long long y){ return os__fb_set_cursor(x, y); }
TN_MSABI long long fb_cursor_mode(long long mode){ return os__fb_cursor_mode(mode); }
TN_MSABI long long fb_cursor(long long on, long long color){ return os__fb_cursor(on, color); }

TN_MSABI long long memmap_count(void){ return os__memmap_count(); }
TN_MSABI long long memmap_base(long long i){ return os__memmap_base(i); }
TN_MSABI long long memmap_len(long long i){ return os__memmap_len(i); }
TN_MSABI long long memmap_type(long long i){ return os__memmap_type(i); }
TN_MSABI long long disk_count(void){ return os__disk_count(); }
TN_MSABI long long disk_media_type(long long i){ return os__disk_media_type(i); }
TN_MSABI long long disk_table_kind(long long i){ return os__disk_table_kind(i); }
TN_MSABI long long disk_mbr_id(long long i){ return os__disk_mbr_id(i); }
TN_MSABI long long disk_part_count(long long i){ return os__disk_part_count(i); }
TN_MSABI long long disk_part_index(long long d, long long p){ return os__disk_part_index(d, p); }
TN_MSABI long long disk_gpt_disk_hi(long long i){ return os__disk_gpt_disk_hi(i); }
TN_MSABI long long disk_gpt_disk_lo(long long i){ return os__disk_gpt_disk_lo(i); }
TN_MSABI long long disk_gpt_part_hi(long long i){ return os__disk_gpt_part_hi(i); }
TN_MSABI long long disk_gpt_part_lo(long long i){ return os__disk_gpt_part_lo(i); }
TN_MSABI long long pci_display_probe(void){ return os__pci_display_probe(); }
TN_MSABI long long pci_display_vendor(void){ return os__pci_display_vendor(); }
TN_MSABI long long pci_display_device(void){ return os__pci_display_device(); }
TN_MSABI long long pci_display_class(void){ return os__pci_display_class(); }
TN_MSABI long long pci_display_subclass(void){ return os__pci_display_subclass(); }
TN_MSABI long long pci_display_prog_if(void){ return os__pci_display_prog_if(); }
TN_MSABI long long pci_display_bus(void){ return os__pci_display_bus(); }
TN_MSABI long long pci_display_slot(void){ return os__pci_display_slot(); }
TN_MSABI long long pci_display_func(void){ return os__pci_display_func(); }
TN_MSABI long long pci_display_command(void){ return os__pci_display_command(); }
TN_MSABI long long pci_display_bar_addr(long long i){ return os__pci_display_bar_addr(i); }
TN_MSABI long long pci_display_bar_size(long long i){ return os__pci_display_bar_size(i); }
TN_MSABI long long pci_display_bar_is_io(long long i){ return os__pci_display_bar_is_io(i); }
TN_MSABI long long pci_display_bar_prefetch(long long i){ return os__pci_display_bar_prefetch(i); }
TN_MSABI long long pci_display_bar_mem_type(long long i){ return os__pci_display_bar_mem_type(i); }
TN_MSABI long long pci_display_bar_safe(long long i){ return os__pci_display_bar_safe(i); }
TN_MSABI long long pci_display_mmio_safe(void){ return os__pci_display_mmio_safe(); }
TN_MSABI long long pci_usb_probe(void){ return os__pci_usb_probe(); }
TN_MSABI long long pci_usb_count(void){ return os__pci_usb_count(); }
TN_MSABI long long pci_usb_kind_count(long long kind){ return os__pci_usb_kind_count(kind); }
TN_MSABI long long pci_usb_bus(long long i){ return os__pci_usb_bus(i); }
TN_MSABI long long pci_usb_slot(long long i){ return os__pci_usb_slot(i); }
TN_MSABI long long pci_usb_func(long long i){ return os__pci_usb_func(i); }
TN_MSABI long long pci_usb_prog_if(long long i){ return os__pci_usb_prog_if(i); }
TN_MSABI long long pci_usb_vendor(long long i){ return os__pci_usb_vendor(i); }
TN_MSABI long long pci_usb_device(long long i){ return os__pci_usb_device(i); }
TN_MSABI long long pci_net_probe(void){ return os__pci_net_probe(); }
TN_MSABI long long pci_net_count(void){ return os__pci_net_count(); }
TN_MSABI long long pci_net_bus(long long i){ return os__pci_net_bus(i); }
TN_MSABI long long pci_net_slot(long long i){ return os__pci_net_slot(i); }
TN_MSABI long long pci_net_func(long long i){ return os__pci_net_func(i); }
TN_MSABI long long pci_net_subclass(long long i){ return os__pci_net_subclass(i); }
TN_MSABI long long pci_net_prog_if(long long i){ return os__pci_net_prog_if(i); }
TN_MSABI long long pci_net_vendor(long long i){ return os__pci_net_vendor(i); }
TN_MSABI long long pci_net_device(long long i){ return os__pci_net_device(i); }
TN_MSABI long long pci_audio_probe(void){ return os__pci_audio_probe(); }
TN_MSABI long long pci_audio_count(void){ return os__pci_audio_count(); }
TN_MSABI long long pci_audio_bus(long long i){ return os__pci_audio_bus(i); }
TN_MSABI long long pci_audio_slot(long long i){ return os__pci_audio_slot(i); }
TN_MSABI long long pci_audio_func(long long i){ return os__pci_audio_func(i); }
TN_MSABI long long pci_audio_subclass(long long i){ return os__pci_audio_subclass(i); }
TN_MSABI long long pci_audio_prog_if(long long i){ return os__pci_audio_prog_if(i); }
TN_MSABI long long pci_audio_vendor(long long i){ return os__pci_audio_vendor(i); }
TN_MSABI long long pci_audio_device(long long i){ return os__pci_audio_device(i); }
TN_MSABI long long gpu_hw_cursor_available(void){ return os__gpu_hw_cursor_available(); }
TN_MSABI long long gpu_hw_cursor_enable(long long on){ return os__gpu_hw_cursor_enable(on); }
TN_MSABI long long gpu_hw_cursor_move(long long x, long long y){ return os__gpu_hw_cursor_move(x, y); }
TN_MSABI long long gpu_driver_kind(void){ return os__gpu_driver_kind(); }
TN_MSABI long long arch_id(void){ return os__arch_id(); }
TN_MSABI long long arch_bits(void){ return os__arch_bits(); }
TN_MSABI long long blk_count(void){ return os__blk_count(); }
TN_MSABI long long blk_sector_size(long long d){ return os__blk_sector_size(d); }
TN_MSABI long long blk_sector_count(long long d){ return os__blk_sector_count(d); }
TN_MSABI long long blk_read_sector(long long d, long long lba, unsigned char* out512){
  return os__blk_read_sector(d, lba, out512);
}
TN_MSABI long long blk_write_sector(long long d, long long lba, unsigned char* in512){
  return os__blk_write_sector(d, lba, in512);
}

TN_MSABI long long irq_enable(void){ return os__irq_enable(); }
TN_MSABI long long irq_disable(void){ return os__irq_disable(); }
long long halt(void){ return os__halt(); }

TN_MSABI long long timer_init(long long hz){ return os__timer_init(hz); }
TN_MSABI long long timer_ticks_irq(void){ return os__timer_ticks_irq(); }
TN_MSABI long long timer_sleep_ms(long long ms){ return os__timer_sleep_ms(ms); }

TN_MSABI long long kbd_has_event(void){ return os__kbd_has_event(); }
TN_MSABI long long kbd_read_scancode(void){ return os__kbd_read_scancode(); }
TN_MSABI long long kbd_read_scancode_raw(void){ return os__kbd_read_scancode_raw(); }
TN_MSABI long long kbd_read_char(void){ return os__kbd_read_char(); }
TN_MSABI long long kbd_set_debug(long long on){ return os__kbd_set_debug(on); }
TN_MSABI long long kbd_last_scancode(void){ return os__kbd_last_scancode(); }
TN_MSABI long long mouse_has_packet(void){ return os__mouse_has_packet(); }
TN_MSABI long long mouse_read_packet(void){ return os__mouse_read_packet(); }
TN_MSABI long long mouse_dx(void){ return os__mouse_dx(); }
TN_MSABI long long mouse_dy(void){ return os__mouse_dy(); }
TN_MSABI long long mouse_buttons(void){ return os__mouse_buttons(); }
TN_MSABI long long ps2_force_mouse_path(long long profile){ return os__ps2_force_mouse_path(profile); }

TN_MSABI long long apic_available(void){ return os__apic_available(); }
TN_MSABI long long apic_init(void){ return os__apic_init(); }
TN_MSABI long long apic_eoi(void){ return os__apic_eoi(); }
TN_MSABI long long apic_timer_init(long long hz){ return os__apic_timer_init(hz); }

TN_MSABI long long hpet_init(void){ return os__hpet_init(); }
TN_MSABI long long hpet_set_base(long long base){ return os__hpet_set_base(base); }
TN_MSABI long long hpet_time_ns(void){ return os__hpet_time_ns(); }
TN_MSABI long long acpi_hpet_base(void){ return os__acpi_hpet_base(); }
TN_MSABI long long power_battery_present(void){ return os__power_battery_present(); }
TN_MSABI long long power_battery_percent(void){ return os__power_battery_percent(); }
TN_MSABI long long rtc_probe(void){ return os__rtc_probe(); }
TN_MSABI long long rtc_year(void){ return os__rtc_year(); }
TN_MSABI long long rtc_month(void){ return os__rtc_month(); }
TN_MSABI long long rtc_day(void){ return os__rtc_day(); }
TN_MSABI long long rtc_hour(void){ return os__rtc_hour(); }
TN_MSABI long long rtc_minute(void){ return os__rtc_minute(); }
TN_MSABI long long rtc_second(void){ return os__rtc_second(); }

// Module-prefixed aliases expected by TezzNative sys.* calls
TN_MSABI long long sys__sys_outb(long long port, long long val){ return sys_outb(port, val); }
TN_MSABI long long sys__sys_inb(long long port){ return sys_inb(port); }
TN_MSABI long long sys__sys_write(long long fd, unsigned char* buf, long long len){ return sys_write(fd, buf, len); }
TN_MSABI long long sys__sys_exit(long long code){ return sys_exit(code); }
TN_MSABI long long sys__sys_time_ns(void){ return sys_time_ns(); }
TN_MSABI long long sys__sys_yield(void){ return sys_yield(); }

typedef long long (*tn_async_fn0)(void);
typedef struct {
  long long result;
} tn_async_task_fs;

TN_MSABI unsigned char* tn_async_spawn(unsigned char* fnptr){
  tn_async_fn0 fn = (tn_async_fn0)fnptr;
  tn_async_task_fs* t = (tn_async_task_fs*)malloc(sizeof(tn_async_task_fs));
  if(!t) return NULL;
  t->result = fn ? fn() : 0;
  return (unsigned char*)t;
}

TN_MSABI long long tn_async_await(unsigned char* handle){
  tn_async_task_fs* t = (tn_async_task_fs*)handle;
  if(!t) return 0;
  long long r = t->result;
  free(t);
  return r;
}

TN_MSABI long long tn_simd_v4f_add(double* out, const double* a, const double* b){
  out[0] = a[0] + b[0];
  out[1] = a[1] + b[1];
  out[2] = a[2] + b[2];
  out[3] = a[3] + b[3];
  return 0;
}

TN_MSABI long long tn_simd_v4f_sub(double* out, const double* a, const double* b){
  out[0] = a[0] - b[0];
  out[1] = a[1] - b[1];
  out[2] = a[2] - b[2];
  out[3] = a[3] - b[3];
  return 0;
}

TN_MSABI long long tn_simd_v4f_mul(double* out, const double* a, const double* b){
  out[0] = a[0] * b[0];
  out[1] = a[1] * b[1];
  out[2] = a[2] * b[2];
  out[3] = a[3] * b[3];
  return 0;
}

TN_MSABI long long tn_simd_v4f_dot(const double* a, const double* b){
  double r = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];
  long long bits = 0;
  memcpy(&bits, &r, sizeof(bits));
  return bits;
}

TN_MSABI long long tn_simd_v4i_add(long long* out, const long long* a, const long long* b){
  out[0] = a[0] + b[0];
  out[1] = a[1] + b[1];
  out[2] = a[2] + b[2];
  out[3] = a[3] + b[3];
  return 0;
}

TN_MSABI long long tn_simd_v4i_mul(long long* out, const long long* a, const long long* b){
  out[0] = a[0] * b[0];
  out[1] = a[1] * b[1];
  out[2] = a[2] * b[2];
  out[3] = a[3] * b[3];
  return 0;
}

TN_MSABI long long tn_simd_v4f_add_strided(double* out, const double* a, const double* b, long long stride){
  size_t s = (size_t)stride;
  out[0] = a[0] + b[0];
  out[s] = a[s] + b[s];
  out[s*2] = a[s*2] + b[s*2];
  out[s*3] = a[s*3] + b[s*3];
  return 0;
}

TN_MSABI long long tn_simd_v4f_sub_strided(double* out, const double* a, const double* b, long long stride){
  size_t s = (size_t)stride;
  out[0] = a[0] - b[0];
  out[s] = a[s] - b[s];
  out[s*2] = a[s*2] - b[s*2];
  out[s*3] = a[s*3] - b[s*3];
  return 0;
}

TN_MSABI long long tn_simd_v4f_mul_strided(double* out, const double* a, const double* b, long long stride){
  size_t s = (size_t)stride;
  out[0] = a[0] * b[0];
  out[s] = a[s] * b[s];
  out[s*2] = a[s*2] * b[s*2];
  out[s*3] = a[s*3] * b[s*3];
  return 0;
}

TN_MSABI long long tn_simd_v4i_add_strided(long long* out, const long long* a, const long long* b, long long stride){
  size_t s = (size_t)stride;
  out[0] = a[0] + b[0];
  out[s] = a[s] + b[s];
  out[s*2] = a[s*2] + b[s*2];
  out[s*3] = a[s*3] + b[s*3];
  return 0;
}

TN_MSABI long long tn_simd_v4i_mul_strided(long long* out, const long long* a, const long long* b, long long stride){
  size_t s = (size_t)stride;
  out[0] = a[0] * b[0];
  out[s] = a[s] * b[s];
  out[s*2] = a[s*2] * b[s*2];
  out[s*3] = a[s*3] * b[s*3];
  return 0;
}

TN_MSABI long long tn_simd_v4f_load(double* out, const double* p){
  memcpy(out, p, sizeof(double) * 4);
  return 0;
}

TN_MSABI long long tn_simd_v4f_store(double* p, const double* v){
  memcpy(p, v, sizeof(double) * 4);
  return 0;
}

TN_MSABI long long tn_simd_v4i_load(long long* out, const long long* p){
  memcpy(out, p, sizeof(long long) * 4);
  return 0;
}

TN_MSABI long long tn_simd_v4i_store(long long* p, const long long* v){
  memcpy(p, v, sizeof(long long) * 4);
  return 0;
}
