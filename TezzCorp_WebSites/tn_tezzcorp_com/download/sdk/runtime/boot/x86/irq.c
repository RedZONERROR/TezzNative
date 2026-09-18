// runtime/boot/x86/irq.c
// Real 32-bit x86 IDT + PIC/PIT + PS/2 keyboard IRQ path.

#include <stdint.h>

#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__) || defined(TN_WINABI) || defined(TEZZ_UEFI)
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
extern void isr_ex14(void);

typedef struct {
  uint16_t offset_low;
  uint16_t selector;
  uint8_t zero;
  uint8_t type_attr;
  uint16_t offset_high;
} __attribute__((packed)) idt_entry;

typedef struct {
  uint16_t limit;
  uint32_t base;
} __attribute__((packed)) idt_ptr;

static idt_entry g_idt[256];
static idt_ptr g_idtr;

static volatile uint64_t g_ticks = 0;
static volatile uint32_t g_hz = 100;

static volatile uint32_t g_khead = 0;
static volatile uint32_t g_ktail = 0;
static volatile uint8_t g_kshift = 0;
static volatile uint8_t g_kcaps = 0;
static volatile uint8_t g_kext = 0;
static volatile uint8_t g_kdebug = 0;
static volatile uint8_t g_ps2_inited = 0;
static volatile uint8_t g_last_scancode = 0;
static volatile uint8_t g_raw_ext = 0;
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

#define KBUF_SIZE 128
static volatile uint8_t g_kbuf[KBUF_SIZE];

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

static int kbd_process_scancode(uint8_t raw, uint8_t* out);
static void ps2_init(int enable_irq);
static void kraw_push(uint8_t sc);
static void kbuf_push(uint8_t sc);
static void mbuf_push(int16_t dx, int16_t dy, uint8_t btn);

static void kputs(const char* s){
  if(!s) return;
  long long n = 0;
  while(s[n]) n++;
  sys_write(1, (unsigned char*)s, n);
}

static void kputhex32(uint32_t v){
  char buf[11];
  buf[0] = '0';
  buf[1] = 'x';
  for(int i=0;i<8;i++){
    int shift = (7 - i) * 4;
    uint8_t d = (uint8_t)((v >> shift) & 0xF);
    buf[2 + i] = (d < 10) ? (char)('0' + d) : (char)('A' + d - 10);
  }
  buf[10] = 0;
  kputs(buf);
}

static void kputhex8(uint8_t v){
  char buf[5];
  buf[0] = '0';
  buf[1] = 'x';
  uint8_t hi = (uint8_t)((v >> 4) & 0xF);
  uint8_t lo = (uint8_t)(v & 0xF);
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

static inline uint16_t read_cs(void){
  uint16_t cs = 0;
  __asm__ volatile("mov %%cs, %0" : "=r"(cs));
  return cs;
}

static inline uint32_t read_cr2(void){
  uint32_t v = 0;
  __asm__ volatile("mov %%cr2, %0" : "=r"(v));
  return v;
}

static inline void lidt(idt_ptr* idtr){
  __asm__ volatile("lidt (%0)" :: "r"(idtr));
}

static void idt_set_gate(int n, void* handler){
  uint32_t addr = (uint32_t)(uintptr_t)handler;
  g_idt[n].offset_low = (uint16_t)(addr & 0xFFFF);
  g_idt[n].selector = read_cs();
  g_idt[n].zero = 0;
  g_idt[n].type_attr = 0x8E;
  g_idt[n].offset_high = (uint16_t)((addr >> 16) & 0xFFFF);
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
  uint32_t divisor = 1193182u / hz;
  if(divisor == 0) divisor = 1;
  sys_outb(0x43, 0x36);
  sys_outb(0x40, divisor & 0xFF);
  sys_outb(0x40, (divisor >> 8) & 0xFF);
}

static void ps2_wait_in(void){
  for(int i=0;i<100000;i++){
    if((sys_inb(0x64) & 0x02) == 0) return;
  }
}

static void ps2_wait_out(void){
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
    if(st & 0x20) return v;
  }
  return 0;
}

static void ps2_flush(void){
  for(int i=0;i<32;i++){
    if((sys_inb(0x64) & 0x01) == 0) break;
    (void)sys_inb(0x60);
  }
}

static void mouse_process_byte(uint8_t b){
  if(g_mouse_pkt_idx == 0){
    if((b & 0x08) == 0) return;
  }
  g_mouse_pkt[g_mouse_pkt_idx] = b;
  g_mouse_pkt_idx++;
  if(g_mouse_pkt_idx < 3) return;
  g_mouse_pkt_idx = 0;
  uint8_t b0 = g_mouse_pkt[0];
  uint8_t b1 = g_mouse_pkt[1];
  uint8_t b2 = g_mouse_pkt[2];
  if(b0 & 0xC0) return;
  int16_t dx = (int16_t)(int8_t)b1;
  int16_t dy = (int16_t)(int8_t)b2;
  if(dx > 96) dx = 96;
  if(dx < -96) dx = -96;
  if(dy > 96) dy = 96;
  if(dy < -96) dy = -96;
  if((dx > 80 || dx < -80) && (dy > 80 || dy < -80)) return;
  dy = (int16_t)(0 - dy);
  uint8_t btn = (uint8_t)(b0 & 0x07);
  mbuf_push(dx, dy, btn);
}

static void mouse_init_device(void){
  if(g_mouse_inited) return;
  g_mouse_inited = 1;
  ps2_write_cmd(0xA8);
  (void)ps2_aux_write_expect_ack(0xF6);
  (void)ps2_aux_write_expect_ack(0xF3);
  (void)ps2_aux_write_expect_ack(100);
  (void)ps2_aux_write_expect_ack(0xF4);
}

static int mouse_poll_packet_hw(void){
  if(!g_ps2_inited) ps2_init(g_hz != 0);
  uint8_t st = (uint8_t)sys_inb(0x64);
  if((st & 0x01) == 0) return 0;
  uint8_t v = (uint8_t)sys_inb(0x60);
  if(st & 0x20){
    mouse_process_byte(v);
    return 1;
  }
  kraw_push(v);
  uint8_t sc = 0;
  if(kbd_process_scancode(v, &sc)){
    kbuf_push(sc);
  }
  return 0;
}

static void ps2_init(int enable_irq){
  if(g_ps2_inited) return;
  g_ps2_inited = 1;

  // Disable ports while configuring.
  ps2_write_cmd(0xAD);
  ps2_write_cmd(0xA7);
  ps2_flush();

  // Controller config byte.
  ps2_write_cmd(0x20);
  uint8_t cfg = ps2_read_data();
  if(enable_irq){
    cfg |= 0x01;
    cfg |= 0x02;
  } else {
    cfg &= (uint8_t)~0x01;
    cfg &= (uint8_t)~0x02;
  }
  cfg |= 0x40; // translated Set1 output
  ps2_write_cmd(0x60);
  ps2_write_data(cfg);
  ps2_flush();

  // Enable first + second PS/2 ports.
  ps2_write_cmd(0xAE);
  ps2_write_cmd(0xA8);
  ps2_flush();

  // Set typematic to sane default.
  ps2_write_data(0xF3);
  (void)ps2_read_data();
  ps2_write_data(0x20);
  (void)ps2_read_data();

  // Enable scanning.
  ps2_write_data(0xF4);
  (void)ps2_read_data();
  mouse_init_device();
  ps2_flush();
}

static void kraw_push(uint8_t sc){
  uint32_t next = (g_kraw_head + 1) % KBUF_RAW_SIZE;
  if(next == g_kraw_tail) return;
  g_kraw[g_kraw_head] = sc;
  g_kraw_head = next;
}

static int kraw_pop(uint8_t* out){
  if(g_kraw_head == g_kraw_tail) return 0;
  *out = g_kraw[g_kraw_tail];
  g_kraw_tail = (g_kraw_tail + 1) % KBUF_RAW_SIZE;
  return 1;
}

static void kbuf_push(uint8_t sc){
  uint32_t next = (g_khead + 1) % KBUF_SIZE;
  if(next == g_ktail) return;
  g_kbuf[g_khead] = sc;
  g_khead = next;
}

static int kbuf_pop(uint8_t* out){
  if(g_khead == g_ktail) return 0;
  *out = g_kbuf[g_ktail];
  g_ktail = (g_ktail + 1) % KBUF_SIZE;
  return 1;
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

static int is_alpha(char c){
  if(c >= 'a' && c <= 'z') return 1;
  if(c >= 'A' && c <= 'Z') return 1;
  return 0;
}

static char scancode_to_ascii(uint8_t sc, int shift){
  static const char map[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=',8,9,
    'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s',
    'd','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v',
    'b','n','m',',','.','/',0,'*',0,' ',0,0,0,0,0,0,
    0,0,0,0,0,0,0,'7','8','9','-','4','5','6','+','1',
    '2','3','0','.',0,0,0,0,0,0,0,0,0,0,0,0
  };
  static const char map_shift[128] = {
    0,27,'!','@','#','$','%','^','&','*','(',')','_','+',8,9,
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,'A','S',
    'D','F','G','H','J','K','L',':','"','~',0,'|','Z','X','C','V',
    'B','N','M','<','>','?',0,'*',0,' ',0,0,0,0,0,0,
    0,0,0,0,0,0,0,'7','8','9','-','4','5','6','+','1',
    '2','3','0','.',0,0,0,0,0,0,0,0,0,0,0,0
  };
  if(sc >= 128) return 0;
  char base = map[sc];
  char sh = map_shift[sc];
  int use_shift = shift ? 1 : 0;
  if(g_kcaps && is_alpha(base)){
    use_shift = use_shift ? 0 : 1;
  }
  return use_shift ? sh : base;
}

static int kbd_process_scancode(uint8_t raw, uint8_t* out){
  if(raw == 0xE0){
    g_kext = 1;
    return 0;
  }
  if(raw & 0x80){
    uint8_t key = (uint8_t)(raw & 0x7F);
    if(key == 0x2A || key == 0x36) g_kshift = 0;
    if(g_kext) g_kext = 0;
    return 0;
  }
  if(raw == 0x2A || raw == 0x36){
    g_kshift = 1;
    return 0;
  }
  if(raw == 0x3A){
    g_kcaps = g_kcaps ? 0 : 1;
    return 0;
  }
  if(g_kext){
    g_kext = 0;
    return 0;
  }
  if(raw >= 128) return 0;
  if(out) *out = raw;
  g_last_scancode = raw;
  return 1;
}

static int kbd_poll_scancode(uint8_t* out){
  if(!g_ps2_inited) ps2_init(g_hz != 0);
  for(int i=0;i<32;i++){
    uint8_t st = (uint8_t)sys_inb(0x64);
    if((st & 0x01) == 0) return 0;
    uint8_t raw = (uint8_t)sys_inb(0x60);
    if(st & 0x20){
      mouse_process_byte(raw);
      continue;
    }
    kraw_push(raw);
    uint8_t sc = 0;
    if(!kbd_process_scancode(raw, &sc)) continue;
    *out = sc;
    return 1;
  }
  return 0;
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
  uint8_t sc = 0;
  if(kbd_process_scancode(v, &sc)){
    kbuf_push(sc);
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
    uint8_t sc = 0;
    if(kbd_process_scancode(v, &sc)){
      kbuf_push(sc);
    }
  }
  pic_eoi(12);
}

void tn_ex0_handler(void){
  kputs("EX0: divide by zero\n");
  for(;;){ __asm__ volatile("hlt"); }
}

void tn_ex14_handler(uint32_t err){
  g_pf_addr = (uint64_t)read_cr2();
  g_pf_err = (uint64_t)err;
  g_pf_count++;
  if(g_pf_autofix){
    // 32-bit on-demand page mapping is not wired yet.
  }
  kputs("EX14: page fault addr=");
  kputhex32((uint32_t)g_pf_addr);
  kputs(" err=");
  kputhex32((uint32_t)err);
  kputs("\n");
  for(;;){ __asm__ volatile("hlt"); }
}

TN_MSABI long long os__timer_init(long long hz){
  for(int i=0;i<256;i++){
    g_idt[i].offset_low = 0;
    g_idt[i].selector = 0;
    g_idt[i].zero = 0;
    g_idt[i].type_attr = 0;
    g_idt[i].offset_high = 0;
  }
  idt_set_gate(0x00, isr_ex0);
  idt_set_gate(0x0E, isr_ex14);
  idt_set_gate(0x20, isr_irq0);
  idt_set_gate(0x21, isr_irq1);
  idt_set_gate(0x2C, isr_irq12);
  g_idtr.base = (uint32_t)(uintptr_t)&g_idt[0];
  g_idtr.limit = (uint16_t)(sizeof(g_idt) - 1);
  lidt(&g_idtr);

  if(hz < 0){
    // IDT-only mode, keep IRQs off for safe bring-up.
    g_hz = 0;
    return 0;
  }
  if(hz == 0) hz = 100;
  g_hz = (uint32_t)hz;

  ps2_init(1);
  pic_remap();
  pit_init((uint32_t)hz);
  return 0;
}

TN_MSABI long long os__timer_ticks_irq(void){
  return (long long)g_ticks;
}

TN_MSABI long long os__timer_sleep_ms(long long ms){
  if(ms <= 0) return 0;
  if(g_hz == 0){
    volatile uint32_t spin = (uint32_t)ms * 4000u;
    while(spin--){ __asm__ volatile(""); }
    return 0;
  }
  uint64_t start = g_ticks;
  uint64_t delta = ((uint64_t)ms * (uint64_t)g_hz + 999ULL) / 1000ULL;
  if(delta == 0) delta = 1;
  while((g_ticks - start) < delta){
    __asm__ volatile("hlt");
  }
  return 0;
}

TN_MSABI long long os__kbd_has_event(void){
  if(!g_ps2_inited) ps2_init(g_hz != 0);
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
  uint8_t raw = 0;
  if(!kraw_pop(&raw)){
    uint8_t sc = 0;
    if(!kbd_poll_scancode(&sc)) return -1;
    if(!kraw_pop(&raw)) return -1;
  }
  if(raw == 0xE0){
    g_raw_ext = 1;
    return 0xE0;
  }
  if(raw & 0x80){
    if(g_raw_ext) g_raw_ext = 0;
    return -1;
  }
  if(g_raw_ext){
    g_raw_ext = 0;
    return (long long)raw;
  }
  return (long long)raw;
}

TN_MSABI long long os__kbd_read_char(void){
  uint8_t sc = 0;
  if(!kbuf_pop(&sc)){
    if(!kbd_poll_scancode(&sc)) return -1;
  }
  kdebug_scancode(sc);
  char ch = scancode_to_ascii(sc, g_kshift ? 1 : 0);
  if(ch == 0) return -1;
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
  if(!g_ps2_inited) ps2_init(g_hz != 0);
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
  if(!got) return 0;
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
  (void)profile;
  if(!g_ps2_inited) ps2_init(g_hz != 0);
  mouse_init_device();
  for(int i=0;i<64;i++){
    uint8_t st = (uint8_t)sys_inb(0x64);
    if((st & 0x01) == 0) break;
    uint8_t v = (uint8_t)sys_inb(0x60);
    if(st & 0x20){
      mouse_process_byte(v);
    } else {
      kraw_push(v);
      uint8_t sc = 0;
      if(kbd_process_scancode(v, &sc)){
        kbuf_push(sc);
      }
    }
  }
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
