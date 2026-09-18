// runtime/boot/x86_64/irq.c
// PIC + PIT timer + keyboard IRQ handling for freestanding kernels.

#include <stdint.h>
#if defined(__x86_64__) && (defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__) || defined(TN_WINABI) || defined(TEZZ_UEFI))
#define TN_MSABI __attribute__((ms_abi))
#else
#define TN_MSABI
#endif

extern long long sys_outb(long long port, long long val) TN_MSABI;
extern long long sys_inb(long long port) TN_MSABI;
extern long long sys_write(long long fd, unsigned char* buf, long long len) TN_MSABI;

extern void isr_irq0(void);
extern void isr_irq1(void);
extern void isr_irq12(void);
extern void isr_ex0(void);
extern void isr_ex1(void);
extern void isr_ex2(void);
extern void isr_ex3(void);
extern void isr_ex4(void);
extern void isr_ex5(void);
extern void isr_ex6(void);
extern void isr_ex7(void);
extern void isr_ex8(void);
extern void isr_ex9(void);
extern void isr_ex10(void);
extern void isr_ex11(void);
extern void isr_ex12(void);
extern void isr_ex13(void);
extern void isr_ex14(void);
extern void isr_ex15(void);
extern void isr_ex16(void);
extern void isr_ex17(void);
extern void isr_ex18(void);
extern void isr_ex19(void);
extern void isr_ex20(void);
extern void isr_ex21(void);
extern void isr_ex22(void);
extern void isr_ex23(void);
extern void isr_ex24(void);
extern void isr_ex25(void);
extern void isr_ex26(void);
extern void isr_ex27(void);
extern void isr_ex28(void);
extern void isr_ex29(void);
extern void isr_ex30(void);
extern void isr_ex31(void);

typedef struct {
  uint16_t offset_low;
  uint16_t selector;
  uint8_t ist;
  uint8_t type_attr;
  uint16_t offset_mid;
  uint32_t offset_high;
  uint32_t zero;
} __attribute__((packed)) idt_entry;

typedef struct {
  uint16_t limit;
  uint64_t base;
} __attribute__((packed)) idt_ptr;

static idt_entry g_idt[256];
static idt_ptr g_idtr;
static uint16_t g_idt_selector = 0x08;

static volatile uint64_t g_ticks = 0;
static volatile uint32_t g_hz = 100;
static volatile uint64_t g_lapic_base = 0;
static volatile uint32_t g_khead = 0;
static volatile uint32_t g_ktail = 0;
static volatile uint8_t g_kshift = 0;
static volatile uint8_t g_kcaps = 0;
static volatile uint8_t g_kdebug = 0;
static volatile uint8_t g_ps2_inited = 0;
static volatile uint8_t g_last_scancode = 0;
static volatile uint8_t g_last_char = 0;
static volatile uint64_t g_last_char_tick = 0;
static volatile uint8_t g_scancode_set2 = 0;
static volatile uint8_t g_kbd_translated = 0;
static volatile uint8_t g_break_next = 0;
static volatile uint8_t g_ext_next = 0;
static volatile uint8_t g_key_down[128];
static volatile uint8_t g_krepeat_key = 0;
static volatile uint64_t g_krepeat_tick = 0;
static volatile uint8_t g_krepeat_enabled = 0;
static volatile uint8_t g_raw_ext = 0;
static volatile uint8_t g_raw_break = 0;
static volatile uint8_t g_mouse_inited = 0;
static volatile uint8_t g_mouse_pkt_idx = 0;
static volatile uint8_t g_mouse_pkt[3];
static volatile uint8_t g_mouse_btn_last = 0;
static volatile int16_t g_mouse_dx_last = 0;
static volatile int16_t g_mouse_dy_last = 0;

static volatile uint64_t g_pf_addr = 0;
static volatile uint64_t g_pf_err = 0;
static volatile uint64_t g_pf_count = 0;
static volatile uint8_t g_pf_autofix = 0;

#define PF_POOL_PAGES 64
static unsigned char g_pf_pool[PF_POOL_PAGES * 4096];
static volatile uint32_t g_pf_pool_next = 0;

static int kbd_process_scancode(uint8_t sc, uint8_t* out);
static void ps2_init(void);

#define KBUF_SIZE 128
static volatile uint8_t g_kbuf[KBUF_SIZE];
// Raw scancode ring (includes 0xE0/0xF0 prefixes).
#define KBUF_RAW_SIZE 256
static volatile uint8_t g_kraw[KBUF_RAW_SIZE];
static volatile uint32_t g_kraw_head = 0;
static volatile uint32_t g_kraw_tail = 0;
#define MBUF_SIZE 128
static volatile int16_t g_mdx[MBUF_SIZE];
static volatile int16_t g_mdy[MBUF_SIZE];
static volatile uint8_t g_mbtn[MBUF_SIZE];
static volatile uint32_t g_mhead = 0;
static volatile uint32_t g_mtail = 0;

static void kbuf_push(uint8_t sc){
  uint32_t next = (g_khead + 1) % KBUF_SIZE;
  if(next != g_ktail){
    g_kbuf[g_khead] = sc;
    g_khead = next;
  }
}

static void mbuf_push(int16_t dx, int16_t dy, uint8_t btn){
  uint32_t next = (g_mhead + 1) % MBUF_SIZE;
  if(next == g_mtail){
    g_mtail = (g_mtail + 1) % MBUF_SIZE;
  }
  g_mdx[g_mhead] = dx;
  g_mdy[g_mhead] = dy;
  g_mbtn[g_mhead] = btn;
  g_mhead = next;
}

static int mbuf_pop(int16_t* dx, int16_t* dy, uint8_t* btn){
  if(g_mhead == g_mtail) return 0;
  if(dx) *dx = g_mdx[g_mtail];
  if(dy) *dy = g_mdy[g_mtail];
  if(btn) *btn = g_mbtn[g_mtail];
  g_mtail = (g_mtail + 1) % MBUF_SIZE;
  return 1;
}

static void kraw_push(uint8_t sc){
  uint32_t next = (g_kraw_head + 1) % KBUF_RAW_SIZE;
  if(next != g_kraw_tail){
    g_kraw[g_kraw_head] = sc;
    g_kraw_head = next;
  }
}

static int kraw_pop(uint8_t* out){
  if(g_kraw_head == g_kraw_tail) return 0;
  *out = g_kraw[g_kraw_tail];
  g_kraw_tail = (g_kraw_tail + 1) % KBUF_RAW_SIZE;
  return 1;
}

static void kputs(const char* s){
  if(!s) return;
  long long n = 0;
  while(s[n]) n++;
  sys_write(1, (unsigned char*)s, n);
}

static void kputhex64(uint64_t v){
  char buf[19];
  buf[0] = '0';
  buf[1] = 'x';
  for(int i=0;i<16;i++){
    int shift = (15 - i) * 4;
    uint8_t d = (uint8_t)((v >> shift) & 0xF);
    buf[2 + i] = (d < 10) ? (char)('0' + d) : (char)('A' + d - 10);
  }
  buf[18] = 0;
  kputs(buf);
}

static void kputhex8(uint8_t v){
  char buf[5];
  buf[0] = '0';
  buf[1] = 'x';
  uint8_t hi = (v >> 4) & 0xF;
  uint8_t lo = v & 0xF;
  buf[2] = (hi < 10) ? (char)('0' + hi) : (char)('A' + hi - 10);
  buf[3] = (lo < 10) ? (char)('0' + lo) : (char)('A' + lo - 10);
  buf[4] = 0;
  kputs(buf);
}

static void kdebug_scancode(uint8_t sc){
  if(!g_kdebug) return;
  kputs("SC=");
  kputhex8(sc);
  kputs("\n");
}

static int is_alpha(char c){
  if(c >= 'a' && c <= 'z') return 1;
  if(c >= 'A' && c <= 'Z') return 1;
  return 0;
}

static int kbd_repeat_ok(uint8_t sc){
  if(!g_krepeat_enabled) return 0;
  uint64_t now = g_ticks;
  uint64_t delay = (g_hz > 0) ? (g_hz / 3) : 30;  // ~300ms @100Hz
  uint64_t rate = (g_hz > 0) ? (g_hz / 20) : 5;   // ~50ms @100Hz
  if(delay < 5) delay = 5;
  if(rate < 2) rate = 2;
  if(sc != g_krepeat_key){
    g_krepeat_key = sc;
    g_krepeat_tick = now;
    return 1;
  }
  if((now - g_krepeat_tick) < delay) return 0;
  if((now - g_krepeat_tick) < rate) return 0;
  g_krepeat_tick = now;
  return 1;
}

static inline uint64_t read_cr3(void);

static void* pf_alloc_page(void){
  if(g_pf_pool_next >= PF_POOL_PAGES) return 0;
  unsigned char* p = &g_pf_pool[g_pf_pool_next * 4096];
  g_pf_pool_next++;
  // zero page
  for(int i=0;i<4096;i++) p[i] = 0;
  return p;
}

static int pf_map_page(uint64_t virt){
  uint64_t cr3 = read_cr3();
  uint64_t* pml4 = (uint64_t*)(uintptr_t)(cr3 & ~0xFFFULL);
  if(!pml4) return 0;
  uint64_t v = virt & ~0xFFFULL;
  uint64_t pml4i = (v >> 39) & 0x1FF;
  uint64_t pdpti = (v >> 30) & 0x1FF;
  uint64_t pdi = (v >> 21) & 0x1FF;
  uint64_t pti = (v >> 12) & 0x1FF;

  uint64_t entry = pml4[pml4i];
  uint64_t* pdpt = 0;
  if((entry & 1) == 0){
    pdpt = (uint64_t*)pf_alloc_page();
    if(!pdpt) return 0;
    pml4[pml4i] = ((uint64_t)(uintptr_t)pdpt) | 0x3;
  } else {
    pdpt = (uint64_t*)(uintptr_t)(entry & ~0xFFFULL);
  }
  entry = pdpt[pdpti];
  uint64_t* pd = 0;
  if((entry & 1) == 0){
    pd = (uint64_t*)pf_alloc_page();
    if(!pd) return 0;
    pdpt[pdpti] = ((uint64_t)(uintptr_t)pd) | 0x3;
  } else {
    pd = (uint64_t*)(uintptr_t)(entry & ~0xFFFULL);
  }
  entry = pd[pdi];
  uint64_t* pt = 0;
  if((entry & 1) == 0){
    pt = (uint64_t*)pf_alloc_page();
    if(!pt) return 0;
    pd[pdi] = ((uint64_t)(uintptr_t)pt) | 0x3;
  } else {
    pt = (uint64_t*)(uintptr_t)(entry & ~0xFFFULL);
  }
  if((pt[pti] & 1) == 0){
    void* page = pf_alloc_page();
    if(!page) return 0;
    pt[pti] = ((uint64_t)(uintptr_t)page) | 0x3;
  }
  return 1;
}

static inline void lidt(idt_ptr* idtr){
  __asm__ volatile("lidt (%0)" :: "r"(idtr));
}

static inline uint16_t read_cs(void){
  uint16_t cs = 0;
  __asm__ volatile("mov %%cs, %0" : "=r"(cs));
  return cs;
}

static void idt_set_gate(int n, void* handler){
  uint64_t addr = (uint64_t)handler;
  g_idt[n].offset_low = (uint16_t)(addr & 0xFFFF);
  g_idt[n].selector = g_idt_selector;
  g_idt[n].ist = 0;
  g_idt[n].type_attr = 0x8E;
  g_idt[n].offset_mid = (uint16_t)((addr >> 16) & 0xFFFF);
  g_idt[n].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
  g_idt[n].zero = 0;
}

static inline uint64_t read_cr2(void){
  uint64_t v = 0;
  __asm__ volatile("mov %%cr2, %0" : "=r"(v));
  return v;
}

static inline uint64_t read_cr3(void){
  uint64_t v = 0;
  __asm__ volatile("mov %%cr3, %0" : "=r"(v));
  return v;
}

static inline uint64_t rdmsr(uint32_t msr){
  uint32_t lo, hi;
  __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
  return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t val){
  uint32_t lo = (uint32_t)val;
  uint32_t hi = (uint32_t)(val >> 32);
  __asm__ volatile("wrmsr" :: "c"(msr), "a"(lo), "d"(hi));
}

static void pic_remap(void){
  sys_outb(0x20, 0x11);
  sys_outb(0xA0, 0x11);
  sys_outb(0x21, 0x20);
  sys_outb(0xA1, 0x28);
  sys_outb(0x21, 0x04);
  sys_outb(0xA1, 0x02);
  sys_outb(0x21, 0x01);
  sys_outb(0xA1, 0x01);
  // Unmask IRQ0/IRQ1/IRQ2(cascade) and IRQ12(mouse).
  sys_outb(0x21, 0xF8);
  sys_outb(0xA1, 0xEF);
}

static void pic_eoi(uint8_t irq){
  if(irq >= 8) sys_outb(0xA0, 0x20);
  sys_outb(0x20, 0x20);
}

static void pit_init(uint32_t hz){
  if(hz == 0) hz = 100;
  uint32_t divisor = 1193182 / hz;
  sys_outb(0x43, 0x36);
  sys_outb(0x40, divisor & 0xFF);
  sys_outb(0x40, (divisor >> 8) & 0xFF);
}

static void ps2_wait_in(void){
  // Wait until we can write (input buffer empty).
  for(int i=0;i<100000;i++){
    if((sys_inb(0x64) & 0x02) == 0) return;
  }
}

static void ps2_wait_out(void){
  // Wait until output buffer full.
  for(int i=0;i<100000;i++){
    if((sys_inb(0x64) & 0x01) != 0) return;
  }
}

static void ps2_write_cmd(uint8_t cmd){
  ps2_wait_in();
  sys_outb(0x64, cmd);
}

static void ps2_write_data(uint8_t data){
  ps2_wait_in();
  sys_outb(0x60, data);
}

static void ps2_write_aux(uint8_t data){
  ps2_wait_in();
  sys_outb(0x64, 0xD4);
  ps2_wait_in();
  sys_outb(0x60, data);
}

static uint8_t ps2_read_data(void){
  ps2_wait_out();
  return (uint8_t)sys_inb(0x60);
}

static uint8_t ps2_aux_write_expect_ack(uint8_t data){
  ps2_write_aux(data);
  for(int i=0;i<16;i++){
    uint8_t st = (uint8_t)sys_inb(0x64);
    if((st & 0x01) == 0) continue;
    uint8_t v = (uint8_t)sys_inb(0x60);
    if(st & 0x20){
      return v;
    }
  }
  return 0;
}

static void ps2_flush(void){
  for(int i=0;i<32;i++){
    if((sys_inb(0x64) & 0x01) == 0) break;
    (void)sys_inb(0x60);
  }
}

static int ps2_query_scancode_set(void){
  ps2_write_data(0xF0);
  uint8_t ack = ps2_read_data();
  if(ack != 0xFA) return -1;
  ps2_write_data(0x00);
  ack = ps2_read_data();
  if(ack != 0xFA) return -1;
  return (int)ps2_read_data();
}

static void mouse_process_byte(uint8_t b){
  if(g_mouse_pkt_idx == 0){
    // Bit 3 is always set on first packet byte.
    if((b & 0x08) == 0) return;
  }
  g_mouse_pkt[g_mouse_pkt_idx] = b;
  g_mouse_pkt_idx++;
  if(g_mouse_pkt_idx < 3) return;
  g_mouse_pkt_idx = 0;
  uint8_t b0 = g_mouse_pkt[0];
  uint8_t b1 = g_mouse_pkt[1];
  uint8_t b2 = g_mouse_pkt[2];
  // Drop overflow packet.
  if(b0 & 0xC0) return;
  int16_t dx = (int16_t)(int8_t)b1;
  int16_t dy = (int16_t)(int8_t)b2;
  // Clamp spurious large deltas from unstable/unsynced packet streams.
  if(dx > 96) dx = 96;
  if(dx < -96) dx = -96;
  if(dy > 96) dy = 96;
  if(dy < -96) dy = -96;
  if((dx > 80 || dx < -80) && (dy > 80 || dy < -80)){
    return;
  }
  // PS/2 reports +Y upwards; screen coordinates are +Y downwards.
  dy = (int16_t)(0 - dy);
  uint8_t btn = (uint8_t)(b0 & 0x07);
  mbuf_push(dx, dy, btn);
}

static void mouse_init_device(void){
  if(g_mouse_inited) return;
  g_mouse_inited = 1;
  // Enable aux (second PS/2) device.
  ps2_write_cmd(0xA8);
  // Defaults + sample-rate for smoother movement.
  (void)ps2_aux_write_expect_ack(0xF6);
  (void)ps2_aux_write_expect_ack(0xF3);
  (void)ps2_aux_write_expect_ack(100);
  // Enable streaming.
  (void)ps2_aux_write_expect_ack(0xF4);
}

static int mouse_poll_packet_hw(void){
  if(!g_ps2_inited) ps2_init();
  uint8_t st = (uint8_t)sys_inb(0x64);
  if((st & 0x01) == 0) return 0;
  uint8_t v = (uint8_t)sys_inb(0x60);
  if(st & 0x20){
    mouse_process_byte(v);
    return 1;
  }
  // Keyboard byte arrived while polling mouse: feed keyboard paths.
  kraw_push(v);
  uint8_t out = 0;
  if(kbd_process_scancode(v, &out)){
    kbuf_push(out);
  }
  return 0;
}

static void ps2_init(void){
  if(g_ps2_inited) return;
  g_ps2_inited = 1;
  // Disable both ports.
  ps2_write_cmd(0xAD);
  ps2_write_cmd(0xA7);
  ps2_flush();
  // Read controller config byte.
  ps2_write_cmd(0x20);
  uint8_t cfg = ps2_read_data();
  // Enable IRQ1/IRQ12 only when timer/interrupt mode is active.
  if(g_hz == 0){
    cfg &= ~(0x01); // IRQ1 disable
    cfg &= ~(0x02); // IRQ12 disable
  } else {
    cfg |= 0x01;    // IRQ1 enable
    cfg |= 0x02;    // IRQ12 enable
  }
  cfg |= 0x40;    // translation (Set2->Set1)
  ps2_write_cmd(0x60);
  ps2_write_data(cfg);
  ps2_flush();
  // Read back config to detect translation (set1 translation bit 6).
  ps2_write_cmd(0x20);
  uint8_t cfg2 = ps2_read_data();
  g_kbd_translated = (cfg2 & 0x40) ? 1 : 0;
  // Re-enable keyboard + aux ports.
  ps2_write_cmd(0xAE);
  ps2_write_cmd(0xA8);
  ps2_flush();
  // Reset keyboard and wait for ACK (0xFA) + self-test (0xAA).
  for(int attempt=0; attempt<3; attempt++){
    ps2_write_data(0xFF);
    uint8_t ack = ps2_read_data();
    if(ack == 0xFE) continue; // resend request
    if(ack != 0xFA) break;
    uint8_t self = ps2_read_data();
    if(self == 0xAA) break;
  }
  // If translation is off, prefer native scan code set 2.
  if(!g_kbd_translated){
    ps2_write_data(0xF5); // disable scanning
    (void)ps2_read_data();
    ps2_write_data(0xF0);
    (void)ps2_read_data();
    ps2_write_data(0x02);
    (void)ps2_read_data();
  }
  // Set typematic rate/delay to reduce auto-repeat spam.
  ps2_write_data(0xF3);
  (void)ps2_read_data();
  // Delay=500ms (01b), Rate=24 (slower). Value 0x20 is a good default.
  ps2_write_data(0x20);
  (void)ps2_read_data();
  // Enable scanning.
  ps2_write_data(0xF4);
  (void)ps2_read_data(); // ack (ignore)
  mouse_init_device();
  ps2_flush();
  int set = -1;
  if(!g_kbd_translated){
    set = ps2_query_scancode_set();
  }
  if(g_kbd_translated){
    g_scancode_set2 = 0;
  } else {
    if(set == 2) g_scancode_set2 = 1;
    else if(set == 1) g_scancode_set2 = 0;
    else g_scancode_set2 = 1;
  }
}

void tn_irq0_handler(void){
  g_ticks++;
  pic_eoi(0);
}

void tn_irq1_handler(void){
  uint8_t st = (uint8_t)sys_inb(0x64);
  uint8_t v = (uint8_t)sys_inb(0x60);
  if(st & 0x20){
    mouse_process_byte(v);
    pic_eoi(1);
    return;
  }
  kraw_push(v);
  uint8_t out = 0;
  int ok = kbd_process_scancode(v, &out);
  if(ok){
    kbuf_push(out);
    g_last_scancode = out;
  }
  pic_eoi(1);
}

void tn_irq12_handler(void){
  uint8_t st = (uint8_t)sys_inb(0x64);
  uint8_t v = (uint8_t)sys_inb(0x60);
  if(st & 0x20){
    mouse_process_byte(v);
  } else {
    kraw_push(v);
    uint8_t out = 0;
    if(kbd_process_scancode(v, &out)){
      kbuf_push(out);
      g_last_scancode = out;
    }
  }
  pic_eoi(12);
}

static int kbuf_pop(uint8_t* out){
  if(g_khead == g_ktail) return 0;
  *out = g_kbuf[g_ktail];
  g_ktail = (g_ktail + 1) % KBUF_SIZE;
  return 1;
}

static int kbd_poll_scancode(uint8_t* out){
  if(!g_ps2_inited) ps2_init();
  for(int i=0;i<32;i++){
    uint8_t st = (uint8_t)sys_inb(0x64);
    if((st & 0x01) == 0) return 0;
    uint8_t sc = (uint8_t)sys_inb(0x60);
    if(st & 0x20){
      mouse_process_byte(sc);
      continue;
    }
    kraw_push(sc);
    uint8_t val = 0;
    if(!kbd_process_scancode(sc, &val)) continue;
    *out = val;
    return 1;
  }
  return 0;
}

static char scancode_to_ascii(uint8_t sc, int shift){
  static const char map_set1[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=',8,9,
    'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s',
    'd','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v',
    'b','n','m',',','.','/',0,'*',0,' ',0,0,0,0,0,0,
    0,0,0,0,0,0,0,'7','8','9','-','4','5','6','+','1',
    '2','3','0','.',0,0,0,0,0,0,0,0,0,0,0,0
  };
  static const char map_set1_shift[128] = {
    0,27,'!','@','#','$','%','^','&','*','(',')','_','+',8,9,
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,'A','S',
    'D','F','G','H','J','K','L',':','"','~',0,'|','Z','X','C','V',
    'B','N','M','<','>','?',0,'*',0,' ',0,0,0,0,0,0,
    0,0,0,0,0,0,0,'7','8','9','-','4','5','6','+','1',
    '2','3','0','.',0,0,0,0,0,0,0,0,0,0,0,0
  };
  static const char map_set2[128] = {
    [0x0D] = '\t', [0x0E] = '`', [0x15] = 'q', [0x16] = '1', [0x1A] = 'z',
    [0x1B] = 's',  [0x1C] = 'a', [0x1D] = 'w', [0x1E] = '2', [0x21] = 'c',
    [0x22] = 'x',  [0x23] = 'd', [0x24] = 'e', [0x25] = '4', [0x26] = '3',
    [0x29] = ' ',  [0x2A] = 'v', [0x2B] = 'f', [0x2C] = 't', [0x2D] = 'r',
    [0x2E] = '5',  [0x31] = 'n', [0x32] = 'b', [0x33] = 'h', [0x34] = 'g',
    [0x35] = 'y',  [0x36] = '6', [0x3A] = 'm', [0x3B] = 'j', [0x3C] = 'u',
    [0x3D] = '7',  [0x3E] = '8', [0x41] = ',', [0x42] = 'k', [0x43] = 'i',
    [0x44] = 'o',  [0x45] = '0', [0x46] = '9', [0x49] = '.', [0x4A] = '/',
    [0x4B] = 'l',  [0x4C] = ';', [0x4D] = 'p', [0x4E] = '-', [0x52] = '\'',
    [0x54] = '[',  [0x55] = '=', [0x5A] = '\n', [0x5B] = ']', [0x5D] = '\\',
    [0x66] = 8
  };
  static const char map_set2_shift[128] = {
    [0x0D] = '\t', [0x0E] = '~', [0x15] = 'Q', [0x16] = '!', [0x1A] = 'Z',
    [0x1B] = 'S',  [0x1C] = 'A', [0x1D] = 'W', [0x1E] = '@', [0x21] = 'C',
    [0x22] = 'X',  [0x23] = 'D', [0x24] = 'E', [0x25] = '$', [0x26] = '#',
    [0x29] = ' ',  [0x2A] = 'V', [0x2B] = 'F', [0x2C] = 'T', [0x2D] = 'R',
    [0x2E] = '%',  [0x31] = 'N', [0x32] = 'B', [0x33] = 'H', [0x34] = 'G',
    [0x35] = 'Y',  [0x36] = '^', [0x3A] = 'M', [0x3B] = 'J', [0x3C] = 'U',
    [0x3D] = '&',  [0x3E] = '*', [0x41] = '<', [0x42] = 'K', [0x43] = 'I',
    [0x44] = 'O',  [0x45] = ')', [0x46] = '(', [0x49] = '>', [0x4A] = '?',
    [0x4B] = 'L',  [0x4C] = ':', [0x4D] = 'P', [0x4E] = '_', [0x52] = '"',
    [0x54] = '{',  [0x55] = '+', [0x5A] = '\n', [0x5B] = '}', [0x5D] = '|',
    [0x66] = 8
  };
  if(sc >= 128) return 0;
  char base = 0;
  char sh = 0;
  if(g_scancode_set2){
    base = map_set2[sc];
    sh = map_set2_shift[sc];
  } else {
    base = map_set1[sc];
    sh = map_set1_shift[sc];
  }
  int use_shift = shift ? 1 : 0;
  if(g_kcaps && is_alpha(base)){
    use_shift = use_shift ? 0 : 1;
  }
  return use_shift ? sh : base;
}

static int kbd_process_scancode(uint8_t sc, uint8_t* out){
  // If controller translation is enabled, force Set1 decoding.
  if(g_kbd_translated){
    g_scancode_set2 = 0;
  }
  // If translation is enabled but we thought we were in Set2, fall back to Set1.
  if(g_scancode_set2 && (sc & 0x80) && sc != 0xE0 && sc != 0xF0){
    g_scancode_set2 = 0;
    g_kbd_translated = 1;
  }
  // Auto-detect Set2 if we see a break prefix in translated mode.
  if(!g_kbd_translated && !g_scancode_set2 && sc == 0xF0){
    g_scancode_set2 = 1;
    g_kbd_translated = 0;
    g_break_next = 1;
    return 0;
  }
  if(g_kbd_translated && sc == 0xF0){
    // Ignore spurious Set2 break prefix when translation is on.
    return 0;
  }

  if(g_scancode_set2){
    if(sc == 0xE0){
      g_ext_next = 1;
      return 0;
    }
    if(sc == 0xF0){
      g_break_next = 1;
      return 0;
    }
    if(g_break_next){
      g_break_next = 0;
      if(sc == 0x12 || sc == 0x59) g_kshift = 0;
      if(sc == 0x58) g_kcaps = g_kcaps; // caps lock release ignored
      if(sc < 128) g_key_down[sc] = 0;
      if(g_krepeat_key == sc) g_krepeat_key = 0;
      return 0;
    }
    if(g_ext_next){
      g_ext_next = 0;
      return 0;
    }
    if(sc == 0x12 || sc == 0x59){
      g_kshift = 1;
      if(sc < 128) g_key_down[sc] = 1;
      return 0;
    }
    if(sc == 0x58){
      // caps lock toggle
      g_kcaps = g_kcaps ? 0 : 1;
      if(sc < 128) g_key_down[sc] = 1;
      return 0;
    }
    if(sc >= 128) return 0;
    if(g_key_down[sc]){
      if(!kbd_repeat_ok(sc)) return 0;
    } else {
      g_key_down[sc] = 1;
      g_krepeat_key = sc;
      g_krepeat_tick = g_ticks;
    }
    if(out) *out = sc;
    g_last_scancode = sc;
    return 1;
  }

  // Set1 (translated) handling
  if(sc == 0xE0){
    g_ext_next = 1;
    return 0;
  }
  if(g_ext_next){
    g_ext_next = 0;
    return 0;
  }
  if(sc & 0x80){
    uint8_t key = (uint8_t)(sc & 0x7F);
    if(key == 0x2A || key == 0x36) g_kshift = 0;
    if(key == 0x3A) g_kcaps = g_kcaps; // caps lock release ignored
    if(key < 128) g_key_down[key] = 0;
    if(g_krepeat_key == key) g_krepeat_key = 0;
    return 0;
  }
  if(sc == 0x2A || sc == 0x36){
    g_kshift = 1;
    if(sc < 128) g_key_down[sc] = 1;
    return 0;
  }
  if(sc == 0x3A){
    g_kcaps = g_kcaps ? 0 : 1;
    if(sc < 128) g_key_down[sc] = 1;
    return 0;
  }
  if(sc >= 128) return 0;
  if(g_key_down[sc]){
    if(!kbd_repeat_ok(sc)) return 0;
  } else {
    g_key_down[sc] = 1;
    g_krepeat_key = sc;
    g_krepeat_tick = g_ticks;
  }
  if(out) *out = sc;
  g_last_scancode = sc;
  return 1;
}

TN_MSABI long long os__timer_init(long long hz){
  sys_outb(0x00E9, 'T');
  g_idt_selector = read_cs();
  if(g_idt_selector == 0){
    g_idt_selector = 0x08;
  }
  // build IDT with timer + keyboard
  for(int i=0;i<256;i++){
    g_idt[i].offset_low = 0;
    g_idt[i].selector = 0;
    g_idt[i].ist = 0;
    g_idt[i].type_attr = 0;
    g_idt[i].offset_mid = 0;
    g_idt[i].offset_high = 0;
    g_idt[i].zero = 0;
  }
  sys_outb(0x00E9, 'U');
  idt_set_gate(0x00, isr_ex0);
  idt_set_gate(0x01, isr_ex1);
  idt_set_gate(0x02, isr_ex2);
  idt_set_gate(0x03, isr_ex3);
  idt_set_gate(0x04, isr_ex4);
  idt_set_gate(0x05, isr_ex5);
  idt_set_gate(0x06, isr_ex6);
  idt_set_gate(0x07, isr_ex7);
  idt_set_gate(0x08, isr_ex8);
  idt_set_gate(0x09, isr_ex9);
  idt_set_gate(0x0A, isr_ex10);
  idt_set_gate(0x0B, isr_ex11);
  idt_set_gate(0x0C, isr_ex12);
  idt_set_gate(0x0D, isr_ex13);
  idt_set_gate(0x0E, isr_ex14);
  idt_set_gate(0x0F, isr_ex15);
  idt_set_gate(0x10, isr_ex16);
  idt_set_gate(0x11, isr_ex17);
  idt_set_gate(0x12, isr_ex18);
  idt_set_gate(0x13, isr_ex19);
  idt_set_gate(0x14, isr_ex20);
  idt_set_gate(0x15, isr_ex21);
  idt_set_gate(0x16, isr_ex22);
  idt_set_gate(0x17, isr_ex23);
  idt_set_gate(0x18, isr_ex24);
  idt_set_gate(0x19, isr_ex25);
  idt_set_gate(0x1A, isr_ex26);
  idt_set_gate(0x1B, isr_ex27);
  idt_set_gate(0x1C, isr_ex28);
  idt_set_gate(0x1D, isr_ex29);
  idt_set_gate(0x1E, isr_ex30);
  idt_set_gate(0x1F, isr_ex31);
  idt_set_gate(0x20, isr_irq0);
  idt_set_gate(0x21, isr_irq1);
  idt_set_gate(0x2C, isr_irq12);
  g_idtr.base = (uint64_t)&g_idt[0];
  g_idtr.limit = (uint16_t)(sizeof(g_idt) - 1);
  lidt(&g_idtr);
  sys_outb(0x00E9, 'V');
  if(hz < 0){
    // IDT-only mode for safe bring-up: do not touch PIC/PIT/PS2.
    g_hz = 0;
    return 0;
  }
  if(hz == 0) hz = 100;
  g_hz = (uint32_t)hz;
  sys_outb(0x00E9, 'p');
  ps2_init();
  sys_outb(0x00E9, 'q');
  pic_remap();
  sys_outb(0x00E9, 'r');
  pit_init((uint32_t)hz);
  sys_outb(0x00E9, 's');
  return 0;
}

void tn_ex0_handler(void){
  kputs("EX0: divide by zero\n");
  for(;;){ __asm__ volatile("hlt"); }
}

void tn_ex14_handler(uint64_t err){
  g_pf_addr = read_cr2();
  g_pf_err = err;
  g_pf_count++;
  if(g_pf_autofix){
    if(pf_map_page(g_pf_addr)){
      return;
    }
  }
  kputs("EX14: page fault addr=");
  kputhex64(g_pf_addr);
  kputs(" err=");
  kputhex64(err);
  kputs("\n");
  for(;;){ __asm__ volatile("hlt"); }
}

TN_MSABI long long os__timer_ticks_irq(void){
  return (long long)g_ticks;
}

TN_MSABI long long os__timer_sleep_ms(long long ms){
  if(ms <= 0) return 0;
  uint64_t start = g_ticks;
  uint64_t target = start + ((uint64_t)ms * (uint64_t)g_hz) / 1000;
  while(g_ticks < target){
    __asm__ volatile("hlt");
  }
  return 0;
}

TN_MSABI long long os__kbd_has_event(void){
  if(!g_ps2_inited) ps2_init();
  if(g_khead != g_ktail) return 1;
  uint8_t st = (uint8_t)sys_inb(0x64);
  if((st & 0x01) == 0) return 0;
  if(st & 0x20){
    (void)mouse_poll_packet_hw();
    return (g_khead != g_ktail) ? 1 : 0;
  }
  return 1;
}

TN_MSABI long long os__kbd_read_scancode(void){
  uint8_t sc = 0;
  if(!kbuf_pop(&sc)){
    if(!kbd_poll_scancode(&sc)) return -1;
  }
  kdebug_scancode(sc);
  return (long long)sc;
}

TN_MSABI long long os__kbd_read_scancode_raw(void){
  uint8_t sc = 0;
  for(;;){
    if(!kraw_pop(&sc)) return -1;
    if(!g_scancode_set2) return (long long)sc;
    if(sc == 0xE0){
      g_raw_ext = 1;
      return 0xE0;
    }
    if(sc == 0xF0){
      g_raw_break = 1;
      continue;
    }
    if(g_raw_break){
      g_raw_break = 0;
      g_raw_ext = 0;
      continue;
    }
    if(g_raw_ext){
      g_raw_ext = 0;
      // Translate Set2 extended make codes to Set1 equivalents.
      switch(sc){
        case 0x6B: return 0x4B; // left
        case 0x74: return 0x4D; // right
        case 0x71: return 0x53; // delete
        case 0x7D: return 0x49; // pgup
        case 0x7A: return 0x51; // pgdn
        case 0x6C: return 0x47; // home
        case 0x69: return 0x4F; // end
        case 0x75: return 0x48; // up
        case 0x72: return 0x50; // down
        case 0x70: return 0x52; // insert
        default: return (long long)sc;
      }
    }
    // Non-extended Set2 make codes: map F-keys to Set1 scancodes.
    switch(sc){
      case 0x05: return 0x3B; // F1
      case 0x06: return 0x3C; // F2
      case 0x04: return 0x3D; // F3
      case 0x0C: return 0x3E; // F4
      case 0x03: return 0x3F; // F5
      case 0x0B: return 0x40; // F6
      case 0x83: return 0x41; // F7
      case 0x0A: return 0x42; // F8
      case 0x01: return 0x43; // F9
      case 0x09: return 0x44; // F10
      case 0x78: return 0x57; // F11
      case 0x07: return 0x58; // F12
      default: break;
    }
    return (long long)sc;
  }
}

TN_MSABI long long os__kbd_read_char(void){
  uint8_t sc = 0;
  if(!kbuf_pop(&sc)){
    if(!kbd_poll_scancode(&sc)) return -1;
  }
  kdebug_scancode(sc);
  char ch = scancode_to_ascii(sc, g_kshift ? 1 : 0);
  if(ch == 0){
    // Fallback: try the alternate set if translation detection was wrong.
    uint8_t prev = g_scancode_set2;
    g_scancode_set2 = prev ? 0 : 1;
    ch = scancode_to_ascii(sc, g_kshift ? 1 : 0);
    g_scancode_set2 = prev;
  }
  if(ch == 0) return -1;
  // De-bounce: ignore same char in the same tick (prevents rapid repeats).
  if(g_last_char == (uint8_t)ch && g_last_char_tick == g_ticks){
    return -1;
  }
  g_last_char = (uint8_t)ch;
  g_last_char_tick = g_ticks;
  return (long long)ch;
}

TN_MSABI long long os__kbd_set_debug(long long on){
  g_kdebug = (on != 0) ? 1 : 0;
  return (long long)g_kdebug;
}

TN_MSABI long long os__kbd_last_scancode(void){
  return (long long)g_last_scancode;
}

TN_MSABI long long os__mouse_has_packet(void){
  if(!g_ps2_inited) ps2_init();
  if(g_mhead != g_mtail) return 1;
  for(int i=0;i<16;i++){
    (void)mouse_poll_packet_hw();
    if(g_mhead != g_mtail) return 1;
  }
  return 0;
}

TN_MSABI long long os__mouse_read_packet(void){
  int16_t dx = 0;
  int16_t dy = 0;
  uint8_t btn = 0;
  int got = mbuf_pop(&dx, &dy, &btn);
  if(!got){
    for(int i=0;i<16;i++){
      (void)mouse_poll_packet_hw();
      if(mbuf_pop(&dx, &dy, &btn)){
        got = 1;
        break;
      }
    }
  }
  if(!got){
    return 0;
  }
  g_mouse_dx_last = dx;
  g_mouse_dy_last = dy;
  g_mouse_btn_last = btn;
  return 1;
}

TN_MSABI long long os__mouse_dx(void){
  return (long long)g_mouse_dx_last;
}

TN_MSABI long long os__mouse_dy(void){
  return (long long)g_mouse_dy_last;
}

TN_MSABI long long os__mouse_buttons(void){
  return (long long)g_mouse_btn_last;
}

TN_MSABI long long os__ps2_force_mouse_path(long long profile){
  int mode = (int)profile;
  if(mode < 0) mode = 0;
  if(mode > 3) mode = 3;
  if(!g_ps2_inited) ps2_init();
  // Ensure PIC mask still allows keyboard + mouse IRQ delivery.
  uint8_t m1 = (uint8_t)sys_inb(0x21);
  uint8_t m2 = (uint8_t)sys_inb(0xA1);
  m1 = (uint8_t)(m1 & (uint8_t)~(1u << 1)); // IRQ1
  m1 = (uint8_t)(m1 & (uint8_t)~(1u << 2)); // IRQ2 cascade
  m2 = (uint8_t)(m2 & (uint8_t)~(1u << 4)); // IRQ12
  sys_outb(0x21, m1);
  sys_outb(0xA1, m2);

  // Re-enable aux path + IRQ bits in controller config.
  ps2_write_cmd(0xA8);
  ps2_write_cmd(0x20);
  uint8_t cfg = ps2_read_data();
  cfg |= 0x03; // IRQ1 + IRQ12
  cfg |= 0x40; // translation
  ps2_write_cmd(0x60);
  ps2_write_data(cfg);

  // Force a full mouse device re-init; VBox can drop stream state.
  g_mouse_inited = 0;
  g_mouse_pkt_idx = 0;
  (void)ps2_aux_write_expect_ack(0xF5); // disable stream before profile config
  if(mode == 2){
    (void)ps2_aux_write_expect_ack(0xF3);
    (void)ps2_aux_write_expect_ack(60);
  } else if(mode == 3){
    (void)ps2_aux_write_expect_ack(0xF3);
    (void)ps2_aux_write_expect_ack(200);
  }
  mouse_init_device();
  // Drain stale bytes so first GUI frame sees deterministic packet state.
  for(int i=0;i<64;i++){
    uint8_t st = (uint8_t)sys_inb(0x64);
    if((st & 0x01) == 0) break;
    uint8_t v = (uint8_t)sys_inb(0x60);
    if(st & 0x20){
      mouse_process_byte(v);
    } else {
      kraw_push(v);
      uint8_t out = 0;
      if(kbd_process_scancode(v, &out)){
        kbuf_push(out);
      }
    }
  }
  return 0;
}

TN_MSABI long long os__apic_available(void){
  // Check APIC base MSR (0x1B)
  uint64_t v = rdmsr(0x1B);
  if(v & (1ULL << 11)) return 1;
  return 0;
}

TN_MSABI long long os__apic_init(void){
  uint64_t v = rdmsr(0x1B);
  v |= (1ULL << 11);
  wrmsr(0x1B, v);
  g_lapic_base = v & 0xFFFFF000ULL;
  if(g_lapic_base == 0) return -1;
  volatile uint32_t* lapic = (volatile uint32_t*)(uintptr_t)g_lapic_base;
  // Spurious interrupt vector register: enable APIC (bit 8)
  lapic[0xF0/4] |= 0x100;
  return 0;
}

TN_MSABI long long os__apic_eoi(void){
  if(g_lapic_base == 0) return -1;
  volatile uint32_t* lapic = (volatile uint32_t*)(uintptr_t)g_lapic_base;
  lapic[0xB0/4] = 0;
  return 0;
}

TN_MSABI long long os__apic_timer_init(long long hz){
  if(g_lapic_base == 0){
    if(os__apic_init() != 0) return -1;
  }
  volatile uint32_t* lapic = (volatile uint32_t*)(uintptr_t)g_lapic_base;
  // Divide config: divide by 16
  lapic[0x3E0/4] = 3;
  // LVT timer: vector 0x20, periodic (bit 17)
  lapic[0x320/4] = 0x20000 | 0x20;
  // Initial count (rough placeholder)
  if(hz <= 0) hz = 100;
  lapic[0x380/4] = 0x100000;
  return 0;
}

TN_MSABI long long pf_last_addr_raw(void){
  return (long long)g_pf_addr;
}

TN_MSABI long long pf_last_err_raw(void){
  return (long long)g_pf_err;
}

TN_MSABI long long pf_last_count_raw(void){
  return (long long)g_pf_count;
}

TN_MSABI long long pf_autofix_set_raw(long long on){
  g_pf_autofix = (on != 0) ? 1 : 0;
  return (long long)g_pf_autofix;
}
