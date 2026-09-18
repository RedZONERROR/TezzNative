// runtime/tnrt_kernel_bridge.c
// Bridge for boot/UEFI stubs -> TezzNative main().

#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__) || defined(TN_WINABI) || defined(TEZZ_UEFI)
#define TN_MSABI __attribute__((ms_abi))
#else
#define TN_MSABI
#endif

extern int main(void) TN_MSABI;

static inline void debugcon_putc(char c){
#if defined(__x86_64__) || defined(__i386__)
  unsigned short port = 0x00E9;
  __asm__ volatile("outb %0, %1" : : "a"(c), "Nd"(port));
#else
  (void)c;
#endif
}

static void debugcon_write(const char* s){
  if(!s) return;
  while(*s){
    debugcon_putc(*s++);
  }
}

static void vga_write_raw(const char* s, int row){
  if(!s) return;
#if defined(__x86_64__) || defined(__i386__)
  volatile unsigned char* vga = (volatile unsigned char*)0xB8000;
  int col = 0;
  int idx = row * 80;
  while(*s && col < 80){
    int off = (idx + col) * 2;
    vga[off] = (unsigned char)(*s++);
    vga[off + 1] = 0x0A; // bright green on black
    col++;
  }
#else
  (void)row;
  while(*s){ s++; }
#endif
}

TN_MSABI int tn_kernel_main(void){
  vga_write_raw("TEZZKERN (C)", 1);
  vga_write_raw("TNRT BRIDGE OK", 2);
  debugcon_write("TEZZKRN\n");
  debugcon_write("C0\n");
  debugcon_write("M0\n");
  int rc = main();
  debugcon_write("M1\n");
  debugcon_write("C1\n");
  // Stay alive even if main returns.
  for(;;){
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("hlt");
#endif
  }
  return rc;
}
